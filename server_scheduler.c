#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <mysql/mysql.h>

#define LOG_FILE "server_status.log"
#define DB_HOST "192.168.0.253"
#define DB_PORT 3307
#define DB_USER "today_chicken"
#define DB_PASS "1q2w3e4r"
#define DB_NAME "server_statistic_db"

// CPU 사용량을 측정하는 함수
void get_cpu_usage(float *usage, float *total_usage) {
    static long long last_idle = 0, last_total = 0;
    FILE* file = fopen("/proc/stat", "r");
    if (file == NULL) {
        *usage = -1;
        *total_usage = -1;
        return;
    }

    char buffer[256];
    fgets(buffer, sizeof(buffer), file);
    fclose(file);

    long long user, nice, system, idle, iowait, irq, softirq;
    sscanf(buffer, "cpu %lld %lld %lld %lld %lld %lld %lld", &user, &nice, &system, &idle, &iowait, &irq, &softirq);

    long long idle_time = idle;
    long long total_time = user + nice + system + idle + iowait + irq + softirq;

    if (last_total != 0) {
        *usage = (float)(total_time - last_total - (idle_time - last_idle)) / (total_time - last_total) * 100.0;
    } else {
        *usage = 0.0;
    }

    *total_usage = total_time - idle_time;

    last_total = total_time;
    last_idle = idle_time;
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

// 보조기억장치 사용량을 측정하는 함수 (가상)
void get_db_usage(float *usage, float *total_usage) {
    // 가상 데이터 생성
    *usage = (float)(rand() % 100);
    *total_usage = *usage;
}

// log.txt 파일에 로그를 남기는 함수
void log_usage() {
    printf("logging\n");
    FILE *logfile = fopen(LOG_FILE, "a");
    if (logfile == NULL) {
        perror("Unable to open log file");
        exit(EXIT_FAILURE);
    }

    float cpu_usage, cpu_total_usage;
    float memory_usage, memory_total_usage;
    float db_usage, db_total_usage;

    get_cpu_usage(&cpu_usage, &cpu_total_usage);
    get_memory_usage(&memory_usage, &memory_total_usage);
    get_db_usage(&db_usage, &db_total_usage);

    time_t now;
    time(&now);
    struct tm *local = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local);

    fprintf(logfile, "[%s] CPU: %.2f%%, DB: %.2f(%.2f%%), Memory: %.1fMB(%.2f%%)\n", 
            time_str, cpu_usage, db_total_usage, db_usage, memory_total_usage / 1000, memory_usage);

    fclose(logfile);
    printf("logging done\n");
}

// 로그 파일을 읽고 최대값과 평균값을 계산하는 함수
void calculate_metrics(float *max_cpu, float *avg_cpu, float *max_db, float *avg_db, float *max_mem, float *avg_mem) {
    FILE *logfile = fopen(LOG_FILE, "r");
    if (logfile == NULL) {
        perror("Unable to open log file");
        exit(EXIT_FAILURE);
    }

    fseek(logfile, 0, SEEK_END);
    long file_size = ftell(logfile);
    long pos = file_size;
    int count = 0;
    char buffer[256];
    float cpu_usage, db_usage_abs, db_usage, memory_usage_abs, memory_usage;
    *max_cpu = *max_db = *max_mem = 0;
    *avg_cpu = *avg_db = *avg_mem = 0;

    // Read the file in reverse
    while (pos > 0 && count < 10) {
        fseek(logfile, --pos, SEEK_SET);

        if (fgetc(logfile) == '\n') {
            fgets(buffer, sizeof(buffer), logfile);
            struct tm log_time;
            sscanf(buffer, "[%d-%d-%d %d:%d:%d] CPU: %f%%, DB: %f(%f%%), Memory: %fMB(%f%%)", 
                   &log_time.tm_year, &log_time.tm_mon, &log_time.tm_mday,
                   &log_time.tm_hour, &log_time.tm_min, &log_time.tm_sec,
                   &cpu_usage, &db_usage_abs, &db_usage, &memory_usage_abs, &memory_usage);

            log_time.tm_year -= 1900;
            log_time.tm_mon -= 1;

            if (cpu_usage > *max_cpu) *max_cpu = cpu_usage;
            if (db_usage > *max_db) *max_db = db_usage;
            if (memory_usage > *max_mem) *max_mem = memory_usage;

            *avg_cpu += cpu_usage;
            *avg_db += db_usage;
            *avg_mem += memory_usage;
            count++;
        }

        if (pos == 1) {
            fseek(logfile, 0, SEEK_SET);
            fgets(buffer, sizeof(buffer), logfile);
            struct tm log_time;
            sscanf(buffer, "[%d-%d-%d %d:%d:%d] CPU: %f%%, DB: %f(%f%%), Memory: %fMB(%f%%)", 
                   &log_time.tm_year, &log_time.tm_mon, &log_time.tm_mday,
                   &log_time.tm_hour, &log_time.tm_min, &log_time.tm_sec,
                   &cpu_usage, &db_usage_abs, &db_usage, &memory_usage_abs, &memory_usage);

            log_time.tm_year -= 1900;
            log_time.tm_mon -= 1;

            if (cpu_usage > *max_cpu) *max_cpu = cpu_usage;
            if (db_usage > *max_db) *max_db = db_usage;
            if (memory_usage > *max_mem) *max_mem = memory_usage;

            *avg_cpu += cpu_usage;
            *avg_db += db_usage;
            *avg_mem += memory_usage;
            count++;
            break;
        }
    }

    if (count > 0) {
        *avg_cpu /= count;
        *avg_db /= count;
        *avg_mem /= count;
    }

    fclose(logfile);
}
// void calculate_metrics(float *max_cpu, float *avg_cpu, float *max_db, float *avg_db, float *max_mem, float *avg_mem) {
//     FILE *logfile = fopen(LOG_FILE, "r");
//     if (logfile == NULL) {
//         perror("Unable to open log file");
//         exit(EXIT_FAILURE);
//     }

//     int count = 0;
//     char buffer[256];
//     float cpu_usage, db_usage_abs, db_usage, memory_usage_abs, memory_usage;
//     *max_cpu = *max_db = *max_mem = 0;
//     *avg_cpu = *avg_db = *avg_mem = 0;

//     time_t now;
//     time(&now);
//     struct tm *local = localtime(&now);
//     local->tm_min -= 5;
//     time_t threshold_time = mktime(local);

//     while (fgets(buffer, sizeof(buffer), logfile)) {
//         struct tm log_time;
//         sscanf(buffer, "[%d-%d-%d %d:%d:%d] CPU: %f%%, DB: %f(%f%%), Memory: %fMB(%f%%)", 
//             &log_time.tm_year, &log_time.tm_mon, &log_time.tm_mday,
//             &log_time.tm_hour, &log_time.tm_min, &log_time.tm_sec,
//             &cpu_usage, &db_usage_abs, &db_usage, &memory_usage_abs, &memory_usage);

//         log_time.tm_year -= 1900;
//         log_time.tm_mon -= 1;
//         time_t log_timestamp = mktime(&log_time);

//         if (difftime(log_timestamp, threshold_time) >= 0) {
//             if (cpu_usage > *max_cpu) *max_cpu = cpu_usage;
//             if (db_usage > *max_db) *max_db = db_usage;
//             if (memory_usage > *max_mem) *max_mem = memory_usage;

//             *avg_cpu += cpu_usage;
//             *avg_db += db_usage;
//             *avg_mem += memory_usage;
//             count++;
//         }
//     }

//     if (count > 0) {
//         *avg_cpu /= count;
//         *avg_db /= count;
//         *avg_mem /= count;
//     }

//     fclose(logfile);
// }

float random_float() {
    return ((float)rand() / (float)RAND_MAX) * 100.0;
}

// 결과를 DB에 저장하는 함수
void save_to_db(float max_cpu, float avg_cpu, float max_db, float avg_db, float max_mem, float avg_mem) {
    printf("save_to_db\n");
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        return;
    }
    printf("mysql_real_connect start\n");
    if (mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0) == NULL) {
        fprintf(stderr, "mysql_real_connect() failed\n");
        mysql_close(conn);
        return;
    }
    printf("mysql_real_connect end\n");
    char query[512];
    snprintf(query, sizeof(query),
            "INSERT INTO statistic (tps_avg, tps_max, mem_avg, mem_max, access_count_avg, access_count_max) VALUES (%f, %f, %f, %f, %f, %f)"
            ,random_float(), random_float(), max_mem, avg_mem, random_float(), random_float());
    printf("%s\n", query);
    if (mysql_query(conn, query)) {
        fprintf(stderr, "INSERT error: %s\n", mysql_error(conn));
    }
    printf("auery success\n");
    mysql_close(conn);
}

int main() {
    time_t start_time = time(NULL);
    time_t current_time;

    while (1) {
        log_usage();
        sleep(1);

        current_time = time(NULL);
        if (difftime(current_time, start_time) >= 5) {
            float max_cpu, avg_cpu, max_db, avg_db, max_mem, avg_mem;
            calculate_metrics(&max_cpu, &avg_cpu, &max_db, &avg_db, &max_mem, &avg_mem);
            printf("%f %f\n", avg_mem, max_mem);
            save_to_db(max_cpu, avg_cpu, max_db, avg_db, max_mem, avg_mem);
            start_time = current_time;
        }
    }

    return 0;
}