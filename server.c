// Name: David Shandor	
// Id: 302902705
#ifndef __SERVER
#define __SERVER
#include <pthread.h>
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>  /* standard system types        */
#include <netinet/in.h> /* Internet address structures */
#include <sys/socket.h> /* socket interface functions  */
#include <netdb.h>      /* host to IP resolution            */

#define MAX_REQ 4096
#define MAX_SIZE 512
#define OK 200
#define OK_DIR 201
#define FOUND 302
#define BAD_REQ 400
#define FORBIDDEN 403
#define NOT_FOUND 404
#define INT_S_ERR 500
#define NOT_SUPP 501
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

//define for responses
#define USAGE "Usage: server <port> <pool-size> <max-number-of-request>\n"
#define RESPONSE "HTTP/1.1 %d %s\r\nServer: webserver/1.0\r\nDate: %s\r\n"
#define ERROR_RES "Content-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"
#define FOUND_RES "Location: %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"
#define OK_RES "Content-Type: %s\r\nContent-Length: %d\r\nLast-Modified:%s\r\nConnection: close\r\n\r\n"
#define OK_NO_CT "Content-Length: %d\r\nLast-Modified:%s\r\nConnection: close\r\n\r\n"
#define TABLE_REG "<tr>\r\n<td><A HREF=\"%s\">%s</A></td><td>%s</td>\r\n"
#define TABLE_SRE "<tr>\r\n<td><A HREF=\"%s\">%s</A></td><td>%s</td>\r\n<td>%lu</dt>\r\n</tr>\r\n"
#define CONTENT "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n<BODY><H4>%d %s</H4>\r\n%s\r\n</BODY></HTML>\r\n\r\n"
#define TABLE "<table CELLSPACING=8>\r\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\r\n\r\n%s\r\n\r\n</table>\r\n\r\n<HR>\r\n\r\n<ADDRESS>webserver/1.0</ADDRESS>\r\n\r\n"
#define CONTABLE "<HTML>\r\n<HEAD><TITLE>Index of %s</TITLE></HEAD>\r\n\r\n<BODY>\r\n<H4>Index of %s</H4>\r\n\r\n%s\r\n\t\n</BODY></HTML>\r\n\r\n"

//response struct
typedef struct response
{
    char *path,
        *erros, //error string
        *date,
        *content_type,
        *table,  // for dir respons
        *string, // for content;
        *resp;
    int cont_len, res_len, number; // number- error number,

} response;

//declare functions
void build_res(response *res, int i, int fd);
int job_fn(void *fd);
void init_res(response *res);
void response_fn(int i, int sockfd, response *res);
void analyze_path(const int fd, response *res);
void build_dir(response *res, int fd);
void res_500(int sockfd);
int check_perms(response *res, int j);

char *get_mime_type(char *name)
{
    char *ext = strrchr(name, '.');
    if (!ext)
        return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, ".gif") == 0)
        return "image/gif";
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".css") == 0)
        return "text/css";
    if (strcmp(ext, ".au") == 0)
        return "audio/basic";
    if (strcmp(ext, ".wav") == 0)
        return "audio/wav";
    if (strcmp(ext, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0)
        return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0)
        return "audio/mpeg";
    return NULL;
}

//handle start up erros.
void error(char *str)
{
    perror(str);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if ((argc < 3) || (atoi(argv[1]) < 1024) || (atoi(argv[2]) <= 0) || ((atoi(argv[3]) <= 0)))
    {
        fprintf(stderr, USAGE);
        exit(1);
    }

    // Declaring variables and structs
    int sockfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    //Create treads pool.
    threadpool *pool = create_threadpool(atoi(argv[2]));

    // open main socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        destroy_threadpool(pool);
        error("Error: opening socket\n");
    }

    // INIT server struct.
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    //Bind socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
    {
        destroy_threadpool(pool);
        close(sockfd);
        error("Error: bindind failed.\n");
    }

    //listen to main socket
    if ((listen(sockfd, 25)) != 0)
    {
        destroy_threadpool(pool);
        close(sockfd);
        error("Error: listen.\n");
    }

    int i = 0;
    int size = atoi(argv[3]);

    int *rc = (int *)malloc(size * sizeof(int));
    if (!rc)
    {
        destroy_threadpool(pool);
        close(sockfd);
        error("Error: listen.\n");
    }

    //accept requests from users, and send it to threads.
    while (i < size)
    {
        if ((rc[i] = accept(sockfd, &cli_addr, &clilen)) < 0)
        {
            perror("accept");
            i++;
            continue;
        }
        dispatch(pool, &job_fn, &(rc[i]));
        i++;
    }
    
    free(rc);
    close(sockfd);
    destroy_threadpool(pool);
    return EXIT_SUCCESS;
}
/**
 * thread job function:
 * 1- read from new socket arr[i]
 * 2- validation request
 * 3- response as requeired.
*/
int job_fn(void *fd)
{
   // init variables
    int sockfd = *(int *)fd;
    char buffer[MAX_REQ] = {0};
    char **arg = NULL;
    char *request = NULL;

    // init response struct.
    response *res = (response *)malloc(sizeof(response));
    if (!res)
    {
        res_500(sockfd);
        return -1;
    }
    init_res(res);
    int rc = 0;

    // read client request.
    if ((rc = read(sockfd, buffer, MAX_REQ)) < 0)
    {
        free(res);
        res_500(sockfd);
        return -1;
    }
    // get the first line only.
    request = strtok(buffer, "\r\n");

    arg = (char **)malloc(sizeof(char *) * 3);
    if (!arg)
    {
        free(res);
        res_500(sockfd);
        return -1;
    }
    for (size_t i = 0; i < 3; i++)
        arg[i] = NULL;

    //parse the arr to 3 part
    int i = 0;
    char *temp = NULL;
    temp = strtok(request, " ");
    while (temp != NULL)
    {
        if (i == 3)//if i == 3, we got too much arguments in the first line.
        {
            response_fn(BAD_REQ, sockfd, res);
            free(res);
            free(arg);
            return -1;
        }
        if (strcmp(temp, "") != 0)// skip spare spaces.
        {
            arg[i] = temp;
            i++;
        }
        temp = strtok(NULL, " ");
    }
   
   //check for missing argument in the request.
        if ((arg[0] == NULL) || (arg[1] == NULL) || (arg[2] == NULL))
        {
            response_fn(BAD_REQ, sockfd, res);
            free(res);
            free(arg);
            return -1;
        }
    
    //validation version and that path is not include "//"
    if (((strcmp(arg[2], "HTTP/1.1") != 0) && (strcmp(arg[2], "HTTP/1.0") != 0)) || (strstr(arg[1], "//")))
    {
        response_fn(BAD_REQ, sockfd, res);
        free(res);
        free(arg);
        return -1;
    }

    //validation method.
    if (strcmp(arg[0], "GET") != 0)
    {
        response_fn(NOT_SUPP, sockfd, NULL);
        free(res);
        free(arg);
        return -1;
    }

    res->path = arg[1];
    //check path and send response.
    analyze_path(sockfd, res);

//free memory.
    if (res->path)
        free(res->path);
    if (res->resp)
        free(res->resp);
    if (res->table)
        free(res->table);

    free(arg);
    free(res);

    return 0;
}
/**
 * response to the client as required.
 * this function response as the given parameter.
*/
void response_fn(int i, int sockfd, response *res)
{
    //Init variables and time.
    int rv = 0;
    time_t now;
    char timebuff[128];
    now = time(NULL);
    strftime(timebuff, sizeof(timebuff), RFC1123FMT, gmtime(&now));

    switch (i)
    {
    case OK://File response, include INDEX.HTML
        res->number = 200;
        res->erros = "OK";
        res->date = timebuff;
        res->content_type = get_mime_type(res->path);
        build_res(res, 1, sockfd);
        i = 0;
        break;
    case OK_DIR: //Directory response.
        res->number = 200;
        res->erros = "OK";
        res->date = timebuff;
        res->content_type = "text/html";
        build_res(res, 2, sockfd);
        break;
    case FOUND:// dirctory has been found, missing slash.
        res->number = 302;
        res->erros = "Found";
        res->date = timebuff;
        res->content_type = "text/html";
        res->string = "Directories must end with a slash.";
        build_res(res, 0, sockfd);
        break;
    case BAD_REQ://bad request response 
        res->number = 400;
        res->erros = "Bad Resquest";
        res->date = timebuff;
        res->content_type = "text/html";
        res->string = "Bad Resquest.";
        build_res(res, 0, sockfd);
        break;
    case FORBIDDEN://forbidden response
        res->number = 403;
        res->erros = "Forbidden";
        res->date = timebuff;
        res->content_type = "text/html";
        res->string = "Access denied.";
        build_res(res, 0, sockfd);
        break;
    case NOT_FOUND://file nor directory has been found
        res->number = 404;
        res->erros = "Not Found";
        res->date = timebuff;
        res->content_type = "text/html";
        res->string = "File not found.";
        build_res(res, 0, sockfd);
        break;
    case NOT_SUPP://Method is no GET response.
        res->number = 501;
        res->erros = "Not supported";
        res->date = timebuff;
        res->content_type = "text/html";
        res->string = "Method is not supported.";
        build_res(res, 0, sockfd);
        break;

    default:
        break;
    }
    if (i)// if reqponse any except file.
    {
        rv = write(sockfd, res->resp, res->res_len);
        if (rv < 0)
        {
            perror("write");
            close(sockfd);
            return;
        }
    }
    close(sockfd);
}
/**
 * Check the path, if dir or file and return the rigth function.
*/
void analyze_path(const int fd, response *res)
{
    //Init variables.
    struct stat fs;
    int size = (strlen(res->path)+2);

//add "."to the beginning of the path.
    char *path = (char *)malloc(size);
    if (!path)
    {
        res_500(fd);
        return;
    }
    memset(path, '\0', size);
    path[0] = '.';
    strcpy(path + 1, res->path);
    res->path = path; 

    //path does not exist.
    if (stat(path, &fs) == -1)
    {   
        perror("stat");
        response_fn(NOT_FOUND, fd, res);
        return;
    }
    //path is directory
    else if (S_ISDIR(fs.st_mode))
    {
        // check if last char is '/'
        if (path[strlen(path) - 1] != '/')
        {
            perror("no /");       
            response_fn(FOUND, fd, res);
            return;
        }
        else
        {   //build the directory.
            build_dir(res, fd);
            return;
        }
    }
    else if (S_ISREG(fs.st_mode)) //check if path is a file
    {
        if (check_perms(res, 2) < 0) // check permissions.
        {
            perror("no permissions");
            return;
        }

        // check read permissions to everyones.
        else if (S_IRGRP & fs.st_mode && S_IROTH & fs.st_mode && S_IRUSR & fs.st_mode) 
            response_fn(OK, fd, res);
            
    }
    else // file not regular.
        response_fn(FORBIDDEN, fd, res);
}
/**
 * build the dirctory response as a table.
*/
void build_dir(response *res, int fd)
{
    //Init variables.
    struct stat file;
    struct dirent *dentry;
    char *path = res->path;
    DIR *dir = opendir(path);
    if (!dir)
    {
        free(res->path);
        res_500(fd);
        return;
    }

    int i = 1;
    //search for index, if not exist- count how many entrance there are in the directory
    while ((dentry = readdir(dir)) != NULL)
    {
        if (strcmp("index.html", (dentry->d_name)) == 0)
        {
            if (check_perms(res, 0))
                response_fn(OK, fd, res);// found index.html file in the directory- response it.
            else
                perror("no permission");
            closedir(dir);
            return;
        }
        i++;
    }

    //no index.html- return all the directory as a table.
    char *table = (char *)malloc(sizeof(char) * 512 * i);
    if (!table)
    {
        res_500(fd);
        closedir(dir);
        return;
    }
    memset(table, 0, 512 * i);

    //re-open dir
    closedir(dir);
    dir = opendir(path);

    char time[128] = {0}; // buffer for time formatted string
    char *temp = NULL;
    int t_size = 0, n = 0;     // size of table and sprintf
    int p_size = strlen(path); // size of path.

    //loop througth the directory and add the entrace to the table.
    while ((dentry = readdir(dir)) != NULL)
    {
        temp = (char *)malloc(strlen(dentry->d_name) + p_size + 1); // memory for path + directory entrance.
        if (!temp)
        {
            closedir(dir);
            free(table);
            res_500(fd);
            return;
        }
        strcpy(temp, path);
        strcpy(temp + p_size, dentry->d_name); // add file to path.

        if (stat(temp, &file) < 0) // check if the directory entrance has last modified arrtibute 
        {
            strftime(time, sizeof(time), RFC1123FMT, gmtime(&(file.st_mtim.tv_sec)));
            n = sprintf(table + t_size, TABLE_REG, dentry->d_name, dentry->d_name, time);
        }
        else // there is last modified information
        {
            strftime(time, sizeof(time), RFC1123FMT, gmtime(&(file.st_mtim.tv_sec)));
            n = sprintf(table + t_size, TABLE_SRE, dentry->d_name, dentry->d_name, time, file.st_size);
        }
        t_size += n;
        free(temp);
    }

    closedir(dir);
    //copy date and build full table content.
    int size = strlen(TABLE) + strlen(table);
    temp = (char *)malloc(size + 1);
    if (!temp)
    {
        res_500(fd);
        free(temp);
        free(table);
        closedir(dir);
        return;
    }
    memset(temp, 0, size + 1);
    n = sprintf(temp, TABLE, table);
    res->table = temp;
    res->cont_len = n;

    free(table);

    response_fn(OK_DIR, fd, res);
}
/**
 * build response to the client.
 * 3 response availble: errors, directories and files.
 * 
*/
void build_res(response *res, int i, int fd)
{
    //init variables.
    struct stat fs;
    int n = 0, size = 0;
    char cont[MAX_REQ] = {0};
    char time[128] = {0};
    //main response memory.
    char *temp = (char *)malloc(sizeof(char) * MAX_REQ);
    if (!temp)
    {
        res_500(fd);
        return;
    }
    memset(temp, '\0', MAX_REQ);

    //build first response data (equals at all the response types).
    if ((size = sprintf(temp, RESPONSE, res->number, res->erros, res->date)) <= 0)
    {
        free(temp);
        res_500(fd);
        return;
    }

    switch (i)
    {
    case 0: // errors response building.
        res->cont_len = sprintf(cont, CONTENT, res->number, res->erros, res->number, res->erros, res->string);
        if (res->cont_len <= 0)
        {
            free(temp);
            res_500(fd);
            return;
        }
        n = sprintf(temp + size, ERROR_RES, res->content_type, res->cont_len);
        if (n <= 0)
        {
            free(temp);
            res_500(fd);
            return;
        }
        size += n;
        n = sprintf(temp + size, "%s", cont);
        if (n <= 0)
        {
            free(temp);
            res_500(fd);
            return;
        }
        size += n;
        break;
    case 1: // file response buildin, include INDEX.html
        if (stat(res->path, &fs) == -1)
        {
            free(temp);
            res_500(fd);
            return;
        }
        res->cont_len = fs.st_size;
        strftime(time, 128, RFC1123FMT, gmtime(&fs.st_mtime));
        unsigned char *fcon = (unsigned char *)malloc(sizeof(unsigned char) * (res->cont_len));
        if (fcon == NULL)
        {
            perror("malloc");
            free(temp);
            res_500(fd);
            return;
        }
        int ffd = 0;
        if ((ffd = open(res->path, O_RDONLY, 0)) < 0)
        {
            perror("open");
            free(fcon);
            free(temp);
            res_500(fd);
            return;
        }

        if ((read(ffd, fcon, res->cont_len)) < 0)
        {
            perror("read");
            free(fcon);
            free(temp);
            res_500(fd);
            return;
        }

        if (res->content_type == NULL)
        {
            if ((n = sprintf(temp + size, OK_NO_CT, res->cont_len, time)) < 0)
            {
                perror("sprintf");
                free(fcon);
                free(temp);
                res_500(fd);
                return;
            }
        }
        else
        {
            if ((n = sprintf(temp + size, OK_RES, res->content_type, res->cont_len, time)) < 0)
            {
                perror("sprintf");
                free(fcon);
                free(temp);
                res_500(fd);
                return;
            }
        }
        size += n;
        if ((n = write(fd, temp, size)) < 0)
        {
            perror("write");
            free(temp);
            free(fcon);
            res_500(fd);
            return;
        }
        if ((write(fd, fcon, res->cont_len)) < 0)
        {
            perror("write");
            free(temp);
            free(fcon);
            res_500(fd);
            return;
        }
        free(fcon);
        free(temp);
        close(fd);
        break;
    case 2: // directories response building.
        if (stat(res->path, &fs) == -1)
        {   
            free(temp);
            res_500(fd);
            return;
        }
        strftime(time, 128, RFC1123FMT, gmtime(&fs.st_mtime));
        n = sprintf(temp + size, OK_RES, res->content_type, res->cont_len, time);
        size += n;
        n = sprintf(temp + size, CONTABLE, res->path, res->path, res->table);
        size += n;
        break;
    default:
        break;
    }
    if (i == 1) // we already write to the server 
    {   
        return;
    }
    res->resp = temp;
    res->res_len = size;
}
// INIT response struct.
void init_res(response *res)
{
    res->path = NULL;
    res->res_len = 0;
    res->resp = NULL;
    res->string = NULL;
    res->table = NULL;
    res->cont_len = 0;
    res->content_type = NULL;
    res->date = NULL;
    res->erros = NULL;
    res->number = 0;
}
//function to handle the 500 error (Internal server error)
void res_500(int sockfd)
{
    time_t now;
    char timebuff[128];
    now = time(NULL);
    strftime(timebuff, sizeof(timebuff), RFC1123FMT, gmtime(&now));
    char buff[MAX_REQ] = {0};
    char cont[MAX_SIZE] = {0};
    int i = 500;
    int n = 0, size = 0;
    char *ise = "Internal Server Error";
    char *type = "text/html";
    char *ssse = "Some server side error.";
    //fill the buff
    size = sprintf(buff, RESPONSE, i, ise, timebuff);
    n = sprintf(cont, CONTENT, i, ise, i, ise, ssse);
    n = sprintf(buff + size, ERROR_RES, type, size);
    size += n;
    n = sprintf(buff + size, "%s", cont);
    size += n;
    write(sockfd, buff, size);
    close(sockfd);
}
/**
 * check the permssions of the file/dirctory.
 * for each dirctory in the path (exclude the target directory),
 * check execute permission. 
 * for the target file / directory - check read permission to everyone.
 * 
*/
int check_perms(response *res, int j)
{   //init variables.
    struct stat fs;
    int n = 0, i = 0, size = strlen(res->path);
    if (j != 2)
        size--;
    char *temp = (char *)malloc(size + 2);
    if (!temp)
    {
        perror("malloc");
        return -1;
    }
    memset(temp, '\0', size + 2);
    strncpy(temp, res->path, size); 

    char *new_path = (char *)malloc(size + 2);
    if (!new_path)
    {
        perror("malloc");
        free(temp);
        return -1;
    }
    memset(new_path, '\0', size + 2);

    //start parse the path, and check permmision for each component of the path.
    char *token = strtok(temp, "/");
    while (token != NULL)
    {
        //Update the path.
        strcpy(new_path + n, token);
        // update the indexes
        i += n;
        n = strlen(new_path);
        if (n == size)
        { // program reach the last token.
            if (stat(new_path, &fs) == -1)
            {
                perror("stat");
                free(temp);
                free(new_path);
                return -1;
            }
            if (j == 0)
            { // index.html - update the path to include index.html
                if (!(S_IXOTH & fs.st_mode))
                {
                    free(temp);
                    free(new_path);
                    return -1;
                }
                char ind[] = "/index.html";
                new_path = (char *)realloc(new_path, n + 13);
                if (!new_path)
                {
                    perror("realloc");
                    free(new_path);
                    free(temp);
                    return -1;
                }
                strcpy(new_path + n, ind);
                if (stat(new_path, &fs) == -1)
                {
                    perror("stat");
                    free(temp);
                    free(new_path);
                    return -1;
                }

                if (!(S_IROTH & fs.st_mode))
                {
                    free(temp);
                    free(new_path);
                    return -1;
                }
                free(res->path);
                res->path = new_path;
            }
            else //the target is directory or file - do not change the path. 
            {
                if (!(S_IROTH & fs.st_mode))
                {
                    free(temp);
                    free(new_path);
                    return -1;
                }
                free(new_path);
            }
        }
        else // still in the path, check for execute permission
        {
            strcpy(new_path + i + 1, "/");
            n++;
            //init the struct.
            if (stat(new_path, &fs) == -1)
            {
                perror("stat");
                free(temp);
                free(new_path);
                return -1;
            }
            if (!(S_IXOTH & fs.st_mode))
            {
                free(temp);
                free(new_path);
                return -1;
            }
        }
        token = strtok(NULL, "/");
    }

    free(temp);
    return 1;
}

#endif // __SERVER