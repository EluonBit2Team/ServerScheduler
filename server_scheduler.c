#define _POSIX_SOURCE
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <mysql/mysql.h>
#include <ctype.h>

#define LOG_FILE "server_status.log"
#define DB_HOST "192.168.0.253"
#define DB_PORT 3307
#define DB_USER "today_chicken"
#define DB_PASS "1q2w3e4r"
#define STATIS_DB_NAME "server_statistic_db"
#define LOG_DB_NAME "log_db"

typedef struct statistic {
    int login_user_max;
    double login_user_avg;

    int tps_max;
    double tps_avg;

    double mem_usage_max;
    double mem_usage_avg;
} statistic_t;

void fix_log_time_pairs(MYSQL* log_conn) {
    char SQL_buf[512];

    snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE client_log SET logout_time = NOW() WHERE logout_time IS NULL");
    if (mysql_query(log_conn, SQL_buf)) {
        fprintf(stderr, "UPDATE client_log failed: %s\n", mysql_error(log_conn));
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE server_log SET downtime = NOW() WHERE downtime IS NULL");
    if (mysql_query(log_conn, SQL_buf)) {
        fprintf(stderr, "UPDATE server_log server_status failed: %s\n", mysql_error(log_conn));
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "INSERT INTO server_log (uptime) VALUES (NOW())");
    if (mysql_query(log_conn, SQL_buf)) {
        fprintf(stderr, "UPDATE server_log timestamp failed: %s\n", mysql_error(log_conn));
    }
}

void handle_signal(int sig) {
    if (sig == SIGTERM || sig == SIGKILL || sig == SIGSEGV || sig == SIGABRT) {
        printf("Child received signal %d, terminating\n", sig);
        exit(0);
    }
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGKILL, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

// 메모리 사용량을 측정하는 함수
void get_memory_usage(float *usage, float *total_usage) {
    FILE* file = fopen("/proc/meminfo", "r");
    if (file == NULL) {
        *usage = -1;
        *total_usage = -1;
        return;
    }

    char buffer[256];
    unsigned long long mem_total, mem_free;
    while (fgets(buffer, sizeof(buffer), file)) {
        if (sscanf(buffer, "MemTotal: %llu kB", &mem_total) == 1) {
            *total_usage = (float)mem_total;
        } else if (sscanf(buffer, "MemFree: %llu kB", &mem_free) == 1) {
            *usage = (float)(mem_total - mem_free) * 100 / mem_total;
            *total_usage = (float)(mem_total - mem_free);
        }
    }
    fclose(file);
}

int get_login_user_cnt(MYSQL* log_conn) {
    const static char* LOGIN_USER_CNT_QUERY = "SELECT COUNT(*) AS active_users FROM client_log WHERE login_time IS NOT NULL AND logout_time IS NULL";
    MYSQL_ROW row;
    MYSQL_RES *res = NULL;
    
    if (mysql_query(log_conn, LOGIN_USER_CNT_QUERY)) {
        fprintf(stderr, "INSERT error: %s\n", mysql_error(log_conn));
        return -1;
    }

    res = mysql_store_result(log_conn);
    if (res == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(log_conn));
        return -1;
    }
    if ((row = mysql_fetch_row(res)) == NULL) {
        return -1;
    }
    int result = atoi(row[0]);
    mysql_free_result(res);
    return result;
}

int get_tps(MYSQL* log_conn) {
    const static char* TPS_QUERY = "SELECT COUNT(*) FROM message_log WHERE timestamp >= NOW() - INTERVAL 5 MINUTE";
    MYSQL_ROW row;
    MYSQL_RES *res = NULL;
    
    if (mysql_query(log_conn, TPS_QUERY)) {
        fprintf(stderr, "INSERT error: %s\n", mysql_error(log_conn));
        return -1;
    }

    res = mysql_store_result(log_conn);
    if (res == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(log_conn));
        return -1;
    }
    if ((row = mysql_fetch_row(res)) == NULL) {
        return -1;
    }
    int result = atoi(row[0]);
    mysql_free_result(res);
    return result;
}


// log.txt 파일에 로그를 남기는 함수
void log_usage(int login_user_cnt, int tps, float memory_usage) {
    FILE *logfile = fopen(LOG_FILE, "a");
    if (logfile == NULL) {
        perror("Unable to open log file");
        exit(EXIT_FAILURE);
    }

    time_t now;
    time(&now);
    struct tm *local = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local);

    fprintf(logfile, "[%s] %d %d %.3f%%\n", time_str, login_user_cnt, tps, memory_usage);

    fclose(logfile);
}

int get_statistic(statistic_t* server_statistic) {
    FILE *file = fopen(LOG_FILE, "r");
    if (!file) {
        perror("Failed to open log file");
        return 1;
    }

    int fd = fileno(file);
    if (flock(fd, LOCK_SH) == -1) {
        perror("Failed to lock file");
        fclose(file);
        return 1;
    }

    int login_user_max = 0, tps_max = 0;
    double mem_usage_max = 0.0;
    long login_user_sum = 0, tps_sum = 0;
    double mem_usage_sum = 0.0;
    int count = 0;

    char line[256];
    char last_line[256] = {0};
    struct tm log_time;
    while (fgets(line, sizeof(line), file)) {
        int login_user, tps;
        double mem_usage;
        sscanf(line, "[%d-%d-%d %d:%d:%d] %d %d %lf%%", 
            &log_time.tm_year, &log_time.tm_mon, &log_time.tm_mday,
            &log_time.tm_hour, &log_time.tm_min, &log_time.tm_sec,
            &login_user, &tps, &mem_usage);

        if (login_user > login_user_max) login_user_max = login_user;
        if (tps > tps_max) tps_max = tps;
        if (mem_usage > mem_usage_max) mem_usage_max = mem_usage;

        login_user_sum += login_user;
        tps_sum += tps;
        mem_usage_sum += mem_usage;

        count++;

        // Copy current line to last_line
        strncpy(last_line, line, sizeof(last_line) - 1);
        last_line[sizeof(last_line) - 1] = '\0'; // Ensure null-terminated string
    }

    flock(fd, LOCK_UN);
    fclose(file);

    if (count > 0) {
        server_statistic->login_user_max = login_user_max;
        server_statistic->login_user_avg = (double)login_user_sum / count;
        server_statistic->tps_max = tps_max;
        server_statistic->tps_avg = (double)tps_sum / count;
        server_statistic->mem_usage_max = mem_usage_max;
        server_statistic->mem_usage_avg = mem_usage_sum / count;
    } else {
        printf("No data to process.\n");
    }
    // Clear the log file
    file = fopen(LOG_FILE, "w");
    if (!file) {
        perror("Failed to clear log file");
        return 1;
    }
    
    fd = fileno(file);
    if (flock(fd, LOCK_SH) == -1) {
        perror("Failed to lock file");
        fclose(file);
        return 1;
    }

    if (strlen(last_line) > 0) {
        fprintf(file, "%s", last_line);
    }

    flock(fd, LOCK_UN);
    fclose(file);

    return 0;
}

void save_statistic_to_db(MYSQL* statistic_conn, statistic_t* server_statistic) {
    printf("save_to_db\n");

    char query[512];
    snprintf(query, sizeof(query),
            "INSERT INTO statistic (tps_avg, tps_max, mem_avg, mem_max, login_user_cnt_avg, login_user_cnt_max, log_timestamp) VALUES (%f, %d, %f, %f, %f, %d, NOW())",
            server_statistic->tps_avg, server_statistic->tps_max, 
            server_statistic->mem_usage_avg, server_statistic->mem_usage_max, 
            server_statistic->login_user_avg, server_statistic->login_user_max);
    printf("%s\n", query);
    if (mysql_query(statistic_conn, query)) {
        fprintf(stderr, "INSERT error: %s\n", mysql_error(statistic_conn));
    }
}

int main() {
    time_t start_time = time(NULL);
    time_t current_time;
    MYSQL* statistic_conn = mysql_init(NULL);
    MYSQL* log_conn = mysql_init(NULL);
    
    setup_signal_handlers();
    if (statistic_conn == NULL || log_conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        return -1;
    }
    printf("mysql_real_connect start\n");
    if (mysql_real_connect(statistic_conn, DB_HOST, DB_USER, DB_PASS, STATIS_DB_NAME, DB_PORT, NULL, 0) == NULL) {
        fprintf(stderr, "mysql_real_connect() failed\n");
        mysql_close(statistic_conn);
        return -1;
    }
    if (mysql_real_connect(log_conn, DB_HOST, DB_USER, DB_PASS, LOG_DB_NAME, DB_PORT, NULL, 0) == NULL) {
        fprintf(stderr, "mysql_real_connect() failed\n");
        mysql_close(log_conn);
        return -1;
    }
    printf("mysql_real_connect end\n");

    while (1) {
        sleep(1);

        int login_user_cnt = get_login_user_cnt(log_conn);
        if (login_user_cnt < 0) {
            return -1;
        }
        int tps = get_tps(log_conn);
        if (tps < 0) {
            return -1;
        }
        float memory_usage, memory_total_usage;
        get_memory_usage(&memory_usage, &memory_total_usage);
        log_usage(login_user_cnt, tps, memory_usage);

        current_time = time(NULL);
        if (difftime(current_time, start_time) >= 5) {
            statistic_t server_statistic;
            get_statistic(&server_statistic);
            save_statistic_to_db(statistic_conn, &server_statistic);
            start_time = current_time;
        }
    }

    return 0;
}