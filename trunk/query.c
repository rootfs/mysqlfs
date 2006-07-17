/*
  mysqlfs - MySQL Filesystem
  Copyright (C) 2006 Tsukasa Hamano <code@cuspy.org>
  $Id: query.c,v 1.5 2006/07/17 13:26:52 cuspy Exp $

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>
#include <fuse.h>
#include <mysql.h>

#include "mysqlfs.h"

#define SQL_MAX 10240

int query_getattr(MYSQL *mysql, const char *path, struct stat *stbuf)
{
    int ret;
    char sql[SQL_MAX];
    char esc_path[PATH_MAX * 2];
    MYSQL_RES* result;
    MYSQL_ROW row;

    mysql_real_escape_string(mysql, esc_path, path, strlen(path));

    snprintf(sql, SQL_MAX,
             "SELECT id, mode, UNIX_TIMESTAMP(atime), UNIX_TIMESTAMP(mtime) "
             "FROM fs WHERE path='%s'",
             esc_path);

    fprintf(stderr, "sql=%s\n", sql);

    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "ERROR: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);
    if(!result){
        fprintf(stderr, "ERROR: mysql_store_result()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    if(mysql_num_rows(result) != 1 && mysql_num_fields(result) != 2){
        mysql_free_result(result);
        return -1;
    }
    row = mysql_fetch_row(result);
    if(!row){
        return -1;
    }

    stbuf->st_mode = atoi(row[1]);
    stbuf->st_uid = fuse_get_context()->uid;
    stbuf->st_gid = fuse_get_context()->gid;
    stbuf->st_nlink = 1;
    stbuf->st_atime = atol(row[2]);
    stbuf->st_mtime = atol(row[3]);

    mysql_free_result(result);

    return 0;
}

int query_inode(MYSQL *mysql, const char *path)
{
    int ret;
    char esc_path[PATH_MAX * 2];
    char sql[SQL_MAX];
    MYSQL_RES* result;
    MYSQL_ROW row;

    mysql_real_escape_string(mysql, esc_path, path, strlen(path));
    snprintf(sql, SQL_MAX, "SELECT id FROM fs WHERE path = '%s'",
             esc_path);

    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "ERROR: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);
    if(!result){
        fprintf(stderr, "ERROR: mysql_store_result()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    if(mysql_num_rows(result) != 1 && mysql_num_fields(result) != 1){
        mysql_free_result(result);
        return -1;
    }

    row = mysql_fetch_row(result);
    if(!row){
        return -1;
    }
    ret = atoi(row[0]);
    mysql_free_result(result);

    return ret;
}

int query_mknod(MYSQL *mysql, const char *path, mode_t mode, dev_t rdev,
                int parent)
{
    int ret;
    char esc_path[PATH_MAX * 2];
    char sql[SQL_MAX];
    MYSQL_RES* result;

    mysql_real_escape_string(mysql, esc_path, path, strlen(path));
    snprintf(sql, SQL_MAX,
             "INSERT INTO fs(path, mode, parent, atime, ctime, mtime, data)"
             "VALUES('%s', %d, %d, 0, 0, 0, \"\")",
             esc_path, S_IFREG | mode, parent);

    printf("sql=%s\n", sql);
    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "ERROR: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);

    printf("mysql_affected_rows()=%llu\n", mysql_affected_rows(mysql));

    mysql_free_result(result);

    return ret;
}

int query_mkdir(MYSQL *mysql, const char *path, mode_t mode, int parent)
{
    int ret;
    char esc_path[PATH_MAX * 2];
    char sql[SQL_MAX];
    MYSQL_RES* result;

    mysql_real_escape_string(mysql, esc_path, path, strlen(path));
    snprintf(sql, sizeof(sql),
             "INSERT INTO fs(path, mode, parent)"
             "VALUES('%s', %d, %d)",
             esc_path, S_IFDIR | mode, parent);

    printf("sql=%s\n", sql);
    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "ERROR: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);

    printf("mysql_affected_rows()=%llu\n", mysql_affected_rows(mysql));

    mysql_free_result(result);

    return ret;
}

int query_readdir(MYSQL *mysql, int inode, void *buf, fuse_fill_dir_t filler)
{
    int ret;
    char sql[SQL_MAX];
    MYSQL_RES* result;
    MYSQL_ROW row;

    snprintf(sql, sizeof(sql), "SELECT path FROM fs WHERE parent = '%d'",
             inode);

    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "ERROR: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);
    if(!result){
        fprintf(stderr, "ERROR: mysql_store_result()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    while((row = mysql_fetch_row(result)) != NULL){
        filler(buf, (char*)basename(row[0]), NULL, 0);
    }

    mysql_free_result(result);

    return ret;
}

int query_delete(MYSQL *mysql, const char *path)
{
    int ret;
    char esc_path[PATH_MAX * 2];
    char sql[SQL_MAX];
    MYSQL_RES* result;

    mysql_real_escape_string(mysql, esc_path, path, strlen(path));
    snprintf(sql, SQL_MAX,
             "DELETE FROM fs WHERE path='%s'", esc_path);

    printf("sql=%s\n", sql);
    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "ERROR: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);
    mysql_free_result(result);

    return ret;
}

int query_utime(MYSQL *mysql, const char *path, struct utimbuf *time){
    int ret;
    char esc_path[PATH_MAX * 2];
    char sql[SQL_MAX];

    fprintf(stderr, "atime=%s\n", ctime(&time->actime));
    fprintf(stderr, "mtime=%s\n", ctime(&time->modtime));

    mysql_real_escape_string(mysql, esc_path, path, strlen(path));

    snprintf(sql, SQL_MAX,
             "UPDATE fs SET "
             "atime=FROM_UNIXTIME(%ld), mtime=FROM_UNIXTIME(%ld) "
             "WHERE path='%s'",
             time->actime, time->modtime, esc_path);

    fprintf(stderr, "sql=%s\n", sql);

    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "Error: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    return 0;
}

int query_read(MYSQL *mysql, const char *path, const char *buf, size_t size,
               off_t offset)
{
    int ret;
    char sql[SQL_MAX];
    char esc_path[PATH_MAX * 2];
    MYSQL_RES* result;
    MYSQL_ROW row;
    unsigned long length;

    mysql_real_escape_string(mysql, esc_path, path, strlen(path));

    snprintf(sql, SQL_MAX,
             "SELECT SUBSTRING(data, %lld, %d) FROM fs WHERE path='%s'",
             offset + 1, size, esc_path);

    //printf("sql=%s\n", sql);

    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "ERROR: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);
    if(!result){
        fprintf(stderr, "ERROR: mysql_store_result()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    if(mysql_num_rows(result) != 1 && mysql_num_fields(result) != 1){
        mysql_free_result(result);
        return -1;
    }
    
    row = mysql_fetch_row(result);
    if(!row){
        mysql_free_result(result);
        return -1;
    }
    
    length = mysql_fetch_lengths(result)[0];
    memcpy((void*)buf, row[0], length);

    mysql_free_result(result);

    return length;
}

int query_write(MYSQL *mysql, const char *path, const char *buf, size_t size,
                off_t offset)
{
    int ret;
    char path_esc[PATH_MAX * 2];
    char sql[SQL_MAX];
    char *data_esc;

    MYSQL_RES* result;

    data_esc = (char*)malloc(SQL_MAX);
    if(!data_esc){
        return -1;
    }

    mysql_real_escape_string(mysql, path_esc, path, strlen(path));
    mysql_real_escape_string(mysql, data_esc, buf, size);

    if(!offset){
        snprintf(sql, SQL_MAX,
                 "UPDATE fs SET data='%s' "
                 "WHERE path='%s'",
                 data_esc, path_esc);
    }else{
        snprintf(sql, sizeof(sql),
                 "UPDATE fs SET data=CONCAT(data, '%s') "
                 "WHERE path='%s'",
                 data_esc, path_esc);
    }
    //printf("sql=%s\n", sql);

    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "ERROR: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);

    if(mysql_affected_rows(mysql) == 1){
        /* success */
    }

    mysql_free_result(result);
    return size;
}

off_t query_size(MYSQL *mysql, const char *path)
{
    off_t ret;
    char path_esc[PATH_MAX * 2];
    char sql[SQL_MAX];
    MYSQL_RES *result;
    MYSQL_ROW row;

    mysql_real_escape_string(mysql, path_esc, path, strlen(path));
    snprintf(sql, SQL_MAX, "SELECT LENGTH(data) FROM fs WHERE path = '%s'",
             path_esc);

    ret = mysql_query(mysql, sql);
    if(ret){
        fprintf(stderr, "ERROR: mysql_query()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    result = mysql_store_result(mysql);
    if(!result){
        fprintf(stderr, "ERROR: mysql_store_result()\n");
        fprintf(stderr, "mysql_error: %s\n", mysql_error(mysql));
        return -1;
    }

    if(mysql_num_rows(result) != 1 && mysql_num_fields(result) != 1){
        mysql_free_result(result);
        return -1;
    }

    row = mysql_fetch_row(result);
    if(!row){
        return -1;
    }

    if(row[0]){
        ret = atoll(row[0]);
    }else{
        ret = 0;
    }
    mysql_free_result(result);

    return ret;
}
