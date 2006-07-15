/*
  mysqlfs - MySQL Filesystem
  Copyright (C) 2006 Tsukasa Hamano <code@cuspy.org>
  $Id: mysqlfs.c,v 1.4 2006/07/15 19:28:09 cuspy Exp $

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>
#include <fuse.h>
#include <mysql.h>
#include <pthread.h>

#ifdef DEBUG
#include <mcheck.h>
#endif

#include "query.h"
#include "pool.h"

static MYSQL_POOL* mysql_pool;

static int mysqlfs_getattr(const char *path, struct stat *stbuf)
{
    int ret;
    MYSQL_CONN *conn;

    fprintf(stderr, "mysqlfs_getattr(\"%s\")\n", path);

    memset(stbuf, 0, sizeof(struct stat));

    conn = mysqlfs_pool_get(mysql_pool);
    if(!conn){
        fprintf(stderr, "Error: mysqlfs_pool_get()\n");
        return -EMFILE;
    }

    ret = query_getattr(conn->mysql, path, stbuf);
    if(ret){
        fprintf(stderr, "Error: query_getattr()\n");
        mysqlfs_pool_return(mysql_pool, conn);
        return -ENOENT;
    }

    stbuf->st_size = query_size(conn->mysql, path);

    mysqlfs_pool_return(mysql_pool, conn);

    return 0;
}

static int mysqlfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;
    int ret;
    MYSQL_CONN *conn;
    int inode;

    fprintf(stderr, "mysqlfs_readdir(\"%s\")\n", path);

    conn = mysqlfs_pool_get(mysql_pool);
    if(!conn){
        fprintf(stderr, "Error: mysqlfs_pool_get()\n");
        return -EMFILE;
    }

    inode = query_inode(conn->mysql, path);
    if(inode < 1){
        fprintf(stderr, "Error: query_inode()\n");
        mysqlfs_pool_return(mysql_pool, conn);
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    ret = query_readdir(conn->mysql, inode, buf, filler);
    mysqlfs_pool_return(mysql_pool, conn);

    return 0;
}

static int mysqlfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int ret;
    MYSQL_CONN *conn;
    int inode;
    char *dir;

    fprintf(stderr, "mysqlfs_mknod(\"%s\")\n", path);

    conn = mysqlfs_pool_get(mysql_pool);
    if(!conn){
        fprintf(stderr, "Error: mysqlfs_pool_get()\n");
        return -EMFILE;
    }

    dir = strdup(path);
    if(!dir){
        fprintf(stderr, "Error: strdup()\n");
        mysqlfs_pool_return(mysql_pool, conn);
        return -ENOENT;
    }

    dirname(dir);
    inode = query_inode(conn->mysql, dir);
    free(dir);

    ret = query_mknod(conn->mysql, path, mode, rdev, inode);

    mysqlfs_pool_return(mysql_pool, conn);
    return 0;
}

static int mysqlfs_mkdir(const char *path, mode_t mode){
    int ret;
    MYSQL_CONN *conn;
    int inode;
    char* dir;

    fprintf(stderr, "mysqlfs_mkdir(\"%s\")\n", path);
    conn = mysqlfs_pool_get(mysql_pool);
    if(!conn){
        fprintf(stderr, "Error: mysqlfs_pool_get()\n");
        return -EMFILE;
    }

    dir = strdup(path);
    if(!dir){
        fprintf(stderr, "Error: strdup()\n");
        mysqlfs_pool_return(mysql_pool, conn);
        return -ENOENT;
    }

    dirname(dir);
    inode = query_inode(conn->mysql, dir);
    free(dir);
    if(inode < 1){
        fprintf(stderr, "Error: query_inode()\n");
        mysqlfs_pool_return(mysql_pool, conn);
        return -ENOENT;
    }

    ret = query_mkdir(conn->mysql, path, mode, inode);
    if(ret){
        fprintf(stderr, "Error: query_mkdir()\n");
        mysqlfs_pool_return(mysql_pool, conn);
        return -ENOENT;
    }

    mysqlfs_pool_return(mysql_pool, conn);

    return 0;
}

static int mysqlfs_unlink(const char *path){
    MYSQL_CONN *conn;

    fprintf(stderr, "mysqlfs_unlink(\"%s\")\n", path);
    conn = mysqlfs_pool_get(mysql_pool);
    if(!conn){
        fprintf(stderr, "Error: mysqlfs_pool_get()\n");
        return -EMFILE;
    }

    query_delete(conn->mysql, path);

    mysqlfs_pool_return(mysql_pool, conn);

    return 0;
}

static int mysqlfs_rmdir(const char *path){
    MYSQL_CONN *conn;

    fprintf(stderr, "mysqlfs_rmdir(\"%s\")\n", path);
    conn = mysqlfs_pool_get(mysql_pool);
    query_delete(conn->mysql, path);
    mysqlfs_pool_return(mysql_pool, conn);

    return 0;
}

static int mysqlfs_flush(const char *path, struct fuse_file_info *fi)
{
    fprintf(stderr, "mysql_flush(\"%s\")\n", path);
    return 0;
}

static int mysqlfs_truncate(const char* path, off_t off)
{
    fprintf(stderr, "mysql_truncate(\"%s\")\n", path);
    printf("off=%lld\n", off);
    return 0;
}

static int mysqlfs_utime(const char *path, struct utimbuf *time){

    fprintf(stderr, "mysql_utime(\"%s\")\n", path);
    fprintf(stderr, "atime=%s", ctime(&time->actime));
    fprintf(stderr, "mtime=%s", ctime(&time->modtime));

    return 0;
}

static int mysqlfs_open(const char *path, struct fuse_file_info *fi)
{
    MYSQL_CONN *conn;
    int inode;

    fprintf(stderr, "mysqlfs_open(\"%s\")\n", path);

    conn = mysqlfs_pool_get(mysql_pool);    
    if(!conn){
        fprintf(stderr, "Error: mysqlfs_pool_get()\n");
        return -EMFILE;
    }

    inode = query_inode(conn->mysql, path);

    if(inode < 1){
        mysqlfs_pool_return(mysql_pool, conn);
        return -ENOENT;
    }

    mysqlfs_pool_return(mysql_pool, conn);

/*
    if((fi->flags & 3) != O_RDONLY)
        return -EACCES;
*/

    return 0;
}

static int mysqlfs_read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi)
{
    int ret;
    MYSQL_CONN *conn;

    fprintf(stderr, "mysqlfs_read(\"%s\")\n", path);
    printf("size=%d\n", size);
    printf("offset=%lld\n", offset);

    conn = mysqlfs_pool_get(mysql_pool);
    if(!conn){
        fprintf(stderr, "Error: mysqlfs_pool_get()\n");
        return -EMFILE;
    }

    ret = query_read(conn->mysql, path, buf, size, offset);
    mysqlfs_pool_return(mysql_pool, conn);

    return ret;
}

static int mysqlfs_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi)
{
    int ret;
    MYSQL_CONN *conn;

    fprintf(stderr, "mysqlfs_write(\"%s\")\n", path);
    printf("size=%d\n", size);
    printf("offset=%lld\n", offset);

    conn = mysqlfs_pool_get(mysql_pool);
    if(!conn){
        fprintf(stderr, "Error: mysqlfs_pool_get()\n");
        return -EMFILE;
    }

    ret = query_write(conn->mysql, path, buf, size, offset);
    mysqlfs_pool_return(mysql_pool, conn);

    return ret;
}

/*
static int mysqlfs_release(const char* path, struct fuse_file_info* fi)
{
    fprintf(stderr, "mysqlfs_release()\n");
    return 0;
}
*/

static struct fuse_operations mysqlfs_oper = {
    .getattr = mysqlfs_getattr,
    .readdir = mysqlfs_readdir,
    .mknod	 = mysqlfs_mknod,
    .mkdir	 = mysqlfs_mkdir,
    .unlink  = mysqlfs_unlink,
    .rmdir   = mysqlfs_rmdir,
    .truncate= mysqlfs_truncate,
    .utime   = mysqlfs_utime,
    .open	 = mysqlfs_open,
    .read	 = mysqlfs_read,
    .write	 = mysqlfs_write,
    .flush   = mysqlfs_flush,
//    .release= mysqlfs_release,
};

void usage(){
    fprintf(stderr,
            "usage: mysqlfs -ohost=host -ouser=user -opasswd=passwd "
            "-odatabase=database ./mountpoint\n");
}

static int mysqlfs_opt_proc(void *data, const char *arg, int key,
                            struct fuse_args *outargs){
    MYSQLFS_OPT *opt = (MYSQLFS_OPT*)data;
    char *str;

    if(key != FUSE_OPT_KEY_OPT){
        fuse_opt_add_arg(outargs, arg);
        return 0;
    }

    if(!strncmp(arg, "host=", strlen("host="))){
        str = strchr(arg, '=') + 1;
        opt->host = str;
        return 0;
    }

    if(!strncmp(arg, "user=", strlen("user="))){
        str = strchr(arg, '=') + 1;
        opt->user = str;
        return 0;
    }

    if(!strncmp(arg, "password=", strlen("password="))){
        str = strchr(arg, '=') + 1;
        opt->passwd = str;
        return 0;
    }

    if(!strncmp(arg, "database=", strlen("database="))){
        str = strchr(arg, '=') + 1;
        opt->db = str;
        return 0;
    }

    if(!strncmp(arg, "connection=", strlen("connection="))){
        str = strchr(arg, '=') + 1;
        opt->connection = atoi(str);
        return 0;
    }

    fuse_opt_add_arg(outargs, arg);
    return 0;
}

/*
 * main
 */
int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    static MYSQLFS_OPT opt;

    /* default param */
    opt.connection = 5;

#ifdef DEBUG
    mtrace();
#endif

    fuse_opt_parse(&args, &opt, NULL, mysqlfs_opt_proc);

    if(!opt.host || !opt.user || !opt.passwd || !opt.db){
        usage();
        fuse_opt_free_args(&args);
        return EXIT_FAILURE;
    }

    mysql_pool = mysqlfs_pool_init(&opt);
    if(!mysql_pool){
        fprintf(stderr, "Error: mysqlfs_pool_init()\n");
        fuse_opt_free_args(&args);
        return EXIT_FAILURE;        
    }

    fuse_main(args.argc, args.argv, &mysqlfs_oper);
    fuse_opt_free_args(&args);

    mysqlfs_pool_print(mysql_pool);
    mysqlfs_pool_free(mysql_pool);

#ifdef DEBUG
    muntrace();
#endif
  
    return EXIT_SUCCESS;
}
