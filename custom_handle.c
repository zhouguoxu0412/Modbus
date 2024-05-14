/***********************************************************************************
Copy right:	    hqyj Tech.
Author:         jiaoyue
Date:           2023.07.01
Description:    http请求处理
***********************************************************************************/

#include <sys/types.h>
#include <sys/socket.h>
#include "custom_handle.h"
#include <sys/shm.h>
#include <sys/ipc.h>
#include<errno.h>
#include<stdint.h>
#include<sys/msg.h>

#define KB 1024
#define HTML_SIZE (64 * KB)
// 定义消息类型
#define LED_ON 0
#define LED_OFF 1
#define BUZZER_ON 2
#define BUZZER_OFF 3

//普通的文本回复需要增加html头部
#define HTML_HEAD "Content-Type: text/html\r\n" \
				  "Connection: close\r\n"
//定义共享内存
typedef struct 
{
   uint16_t dest[32];
}sharedata;
// 定义消息结构体
 struct msgbuf
{
    long mytype; // 消息类型
    int id;     // 从机id
    int status; // 状态
};


static int handle_login(int sock, const char *input)
{
    char reply_buf[HTML_SIZE] = {0};
    char *uname = strstr(input, "username=");
    uname += strlen("username=");
    char *p = strstr(input, "password");
    *(p - 1) = '\0';
    printf("username = %s\n", uname);

    char *passwd = p + strlen("password=");
    printf("passwd = %s\n", passwd);

    if(strcmp(uname, "admin")==0 && strcmp(passwd, "admin")==0)
    {
        sprintf(reply_buf, "<script>localStorage.setItem('usr_user_name', '%s');</script>", uname);
        strcat(reply_buf, "<script>window.location.href = '/index.html';</script>");
        send(sock,reply_buf,strlen(reply_buf),0);
    }
    else
    {
        printf("web login failed\n");

        //"用户名或密码错误"提示，chrome浏览器直接输送utf-8字符流乱码，没有找到太好解决方案，先过渡
        char out[128] = {0xd3,0xc3,0xbb,0xa7,0xc3,0xfb,0xbb,0xf2,0xc3,0xdc,0xc2,0xeb,0xb4,0xed,0xce,0xf3};
        sprintf(reply_buf, "<script charset='gb2312'>alert('%s');</script>", out);
        strcat(reply_buf, "<script>window.location.href = '/login.html';</script>");
        send(sock,reply_buf,strlen(reply_buf),0);
    }

    return 0;
}

static int handle_add(int sock, const char *input)
{
    int number1, number2;
    
    //input必须是"data1=1data2=6"类似的格式，注意前端过来的字符串会有双引号
    sscanf(input, "\"data1=%ddata2=%d\"", &number1, &number2);
    printf("num1 = %d\n", number1);

    char reply_buf[HTML_SIZE] = {0};
    printf("num = %d\n", number1+number2);
    sprintf(reply_buf, "%d", number1+number2);
    printf("resp = %s\n", reply_buf);
    send(sock,reply_buf,strlen(reply_buf),0);

    return 0;
}
//将共享内存中的数据取出并回复给客户端
int handler_get(int sock,const char *input)
{
    key_t key;
    int shmid;
     sharedata *data;

    //创建key值
    key = ftok("shm.c", 'd');
    if (key < 0)
    {
        perror("ftok err");
        return -1;
    }
    printf("key: %#x\n", key);

    //创建或打开共享内存
    shmid = shmget(key, 128, IPC_CREAT | IPC_EXCL | 0666); //没有则创建共享内存，已有则返回-1

    if (shmid <= 0)
    {
        if (errno == EEXIST)                //如果已存在则直接打开共享内存
            shmid = shmget(key, 128, 0666); //直接打开共享内存，返回共享内存id
        else
        {
            perror("shmget err");
            return -1;
        }
    }
    printf("shmid: %d\n", shmid);

    //映射共享内存到用户空间
    data = (sharedata *)shmat(shmid, NULL, 0);
    if (data == (sharedata *)-1)
    {
        perror("shmat err");
        return -1;
    }
   
    char reply_buf[HTML_SIZE]={0};
    sprintf(reply_buf, "%d,%d,%d",data->dest[0],data->dest[1],data->dest[2]);
    send(sock, reply_buf, strlen(reply_buf), 0);

    return 0;
}

//通过消息队列将请求发送到modbus中，控制modbus
int handler_set(const char *input)
{
    //接收postman数据
 int id,status;
 if (sscanf(input, "%d %d", &id, &status) != 2)
    {
        fprintf(stderr, "Invalid input format\n");
        return -1;
    }
    //打开消息队列
    key_t key;
    int msgid;
    key=ftok(".",9);
    if (key < 0)
    {
        perror("ftok err");
        return -1;
    }
    printf("key:%#x\n", key);

    msgid = msgget(key, IPC_CREAT | IPC_EXCL | 0666);
    if (msgid <= 0)
    {
        if (errno == EEXIST)
            msgid = msgget(key, 0666);
        else
        {
            perror("msgget err");
            return -1;
        }
    }
    printf("msgid: %d\n", msgid);
    
    struct msgbuf msg;
    msg.id=id;
    msg.status=status;
    //发送消息到消息队列
    if(msgsnd(msgid,&msg,sizeof(msg)-sizeof(long),0)== -1)
    {
        perror("msgsnd err");
        return -1;
    }
    printf("Sent message: id=%d, status=%d\n", id, status);

    return 0;
}

/**
 * @brief 处理自定义请求，在这里添加进程通信
 * @param input
 * @return
 */
int parse_and_process(int sock, const char *query_string, const char *input)
{
    //query_string不一定能用的到

    //先处理登录操作
    if(strstr(input, "username=") && strstr(input, "password="))
    {
        return handle_login(sock, input);
    }
    //处理求和请求
    else if(strstr(input, "data1=") && strstr(input, "data2="))
    {
        return handle_add(sock, input);
    }
    else if (strstr(input, "get_data"))
    {
        // 调用 handler_get 函数从共享内存中读取数据
       return handler_get(sock,input);
    }
    else if(strstr(input, "0 0") != NULL || strstr(input, "0 1") != NULL || strstr(input, "1 0") != NULL || strstr(input, "1 1") != NULL)
    {
        return handler_set(input);
    }
    else  //剩下的都是json请求，这个和协议有关了
    {
        // 构建要回复的JSON数据
        const char* json_response = "{\"message\": \"Hello, client!\"}";

        // 发送HTTP响应给客户端
        send(sock, json_response, strlen(json_response), 0);
    }

    return 0;
}
