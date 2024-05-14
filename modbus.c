#include <stdio.h>
#include <modbus.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include<stdint.h>
#include<sys/msg.h>
#include<errno.h>
modbus_t *ctx;
//共享内存结构体
typedef struct 
{
    uint16_t dest[32];
}sharedata;
//消息队列结构体
struct msgbuf
{
    long mytype; // 消息类型
    int id;     // 从机id
    int status; // 状态
};

//读取数据的线程函数
void *pthread_read(void *arg)
{
    key_t key;
    int shmid;
    sharedata *data;

    //创建key值
    key = ftok("shm.c", 'd');
    if (key < 0)
    {
        perror("ftok err");
        return NULL;
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
            return NULL;
        }
    }
    printf("shmid: %d\n", shmid);

    //映射共享内存到用户空间
    data = (sharedata *)shmat(shmid, NULL, 0);
    if (data == (sharedata *)-1)
    {
        perror("shmat err");
        return NULL;
    }
    while(1)
     {
      sleep(3);
      modbus_read_registers(ctx, 0, 3, data->dest);
      printf("读到的数据为:");
      for (int i = 0; i < 3; i++)
      printf("%#x", data->dest[i]);
      printf("\n");
     }
     // 分离共享内存段
    shmdt(data);
    // 销毁共享内存
    shmctl(shmid, IPC_RMID, NULL);
    return NULL;
    }

//写入数据的线程函数
void *pthread_write(void *arg)
{
    key_t key;
    int msgid;
    key = ftok(".", 9);
    if (key < 0)
    {
        perror("ftok err");
        return NULL;
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
            return NULL;
        }
    }
    printf("msgid: %d\n", msgid);
    struct msgbuf msg;
    
    while (1)
    {
        //从消息队列中接受数据
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), 0, 0) == -1)
        {
            perror("msgrcv error");
            exit(EXIT_FAILURE);
        }
        printf("请输入从机id及状态:id=%d,status=%d\n",msg.id,msg.status);
        
        if (modbus_write_bit(ctx, msg.id,msg.status) < 0)
        {
            printf("write err\n");
            exit(-1);
        }
    }
}
int main(int argc, char const *argv[])
{
    ctx = modbus_new_tcp("192.168.51.189", 502);
    if (modbus_set_slave(ctx, 1) < 0)
    {
        perror("set slave err");
        return -1;
    }
    if (modbus_connect(ctx) < 0)
    {
        perror("connect err");
        return -1;
    }
    printf("connect success\n");
    pthread_t tid1, tid2;
    if (pthread_create(&tid1, NULL, pthread_read, NULL) != 0)
    {
        perror("tid1 pthread create err");
        return -1;
    }
    if (pthread_create(&tid2, NULL, pthread_write, NULL) != 0)
    {
        perror("tid2 pthread create err");
        return -1;
    }
    pthread_join(tid1,NULL);
    pthread_join(tid2,NULL);
    return 0;
}