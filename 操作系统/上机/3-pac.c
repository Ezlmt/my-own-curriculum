#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define PRODUCER 2  //宏定义生产者个数， 2个
#define COSTOMER 3  //宏定义消费者个数， 3个
#define WRITE_NUM 6 //宏定义写缓冲次数， 6次
#define READ_NUM 4  //宏定义读缓冲次数， 4次

#define SEM_ALL_KEY 1342
#define SEM_EMPTY 0
#define SEM_FULL 1

#define BUF_LENGTH (sizeof(struct container_buffer)) //宏定义缓冲区大小
#define BUFFER_NUM 3                                 //宏定义缓冲区个数 ， 3个
#define SHM_MODE 0600                                //宏定义创建和访问标志

//缓冲区结构（循环队列）
struct container_buffer //定义共享缓冲区结构
{
    char letter[BUFFER_NUM];
    int head;
    int tail;
    int is_empty; //判断缓冲区是否为空的标志
};

//得到6以内的一个随机数，产生延迟时间
int random_num()
{
    int t;
    srand((unsigned)(getpid() + time(NULL)));
    t = rand() % 6;
    return t;
}
// P操作，获得使用权
void p(int sem_id, int sem_num)
{
    struct sembuf sem_buff;
    sem_buff.sem_num = sem_num;
    sem_buff.sem_op = -1;
    sem_buff.sem_flg = 0;
    semop(sem_id, &sem_buff, 1);
}

//得到一个随机字符,模拟产品名字
char random_letter()
{
    char a;
    srand((unsigned)(getpid() + time(NULL)));
    a = (char)((char)(rand() % 15) + '!');
    return a;
}

// V操作，释放使用权
void v(int sem_id, int sem_num)
{
    struct sembuf sem_buff;
    sem_buff.sem_num = sem_num;
    sem_buff.sem_op = 1;
    sem_buff.sem_flg = 0;
    semop(sem_id, &sem_buff, 1);
}

//主函数
int main(int argc, char *argv[])
{
    int shm_id, sem_id;              //定义共享内存段标识变量shm_id，定义信号量标识变量sem_id
    int num_p = 0, num_c = 0, i, j;  //定义生产者和消费者的个数变量，初始化为0
    struct container_buffer *shmptr; //指向缓冲区结构的指针
    char pn;                         //随机字符，代表产品
    time_t now;                      //时间变量
    pid_t pid_p, pid_c;              //进程pid变量
    printf("Main process starts.\n");
    sem_id = semget(SEM_ALL_KEY, 2, IPC_CREAT | 0660); //创建两个信号量, empty, full
    semctl(sem_id, SEM_EMPTY, SETVAL, BUFFER_NUM);
    //索引为SEM_EMPTY的信号量值为3
    semctl(sem_id, SEM_FULL, SETVAL, 0);
    //索引为SEM_FULL的信号量值为0

    if ((shm_id = shmget(IPC_PRIVATE, BUF_LENGTH, SHM_MODE)) < 0)
    //申请一个共享主存段,大小为缓冲区大小
    {
        exit(1); //失败退出
    }
    if ((shmptr = shmat(shm_id, 0, 0)) == (void *)-1) //将共享段与进程相连
    {
        exit(1); //失败退出
    }
    shmptr->head = 0; //初始化缓冲区
    shmptr->tail = 0;
    shmptr->is_empty = 1;

    while ((num_p++) < PRODUCER) //循环创建2个生产者
    {
        if ((pid_p = fork()) < 0) //创建一个进程
        {
            exit(1); //失败退出
        }
        //如果是子进程，开始创建生产者
        if (pid_p == 0)
        {
            if ((shmptr = shmat(shm_id, 0, 0)) == (void *)-1) //将共享段与本进程相连
            {
                exit(1); //失败退出
            }
            for (i = 0; i < WRITE_NUM; i++) //循环尝试在缓冲区内放入数据为WRITE_NUM次
            {
                p(sem_id, SEM_EMPTY); // p操作，申请使用权,p(empty)
                sleep(random_num());  //随机等待一段时间
                shmptr->letter[shmptr->tail] = pn = random_letter();
                //在缓冲队列里面放入一个产品
                shmptr->tail = (shmptr->tail + 1) % BUFFER_NUM;
                shmptr->is_empty = 0; //更新缓冲区状态为满
                now = time(NULL);     //取得系统时间
                printf("current time: %02d:%02d:%02d\t",
                       localtime(&now)->tm_hour, localtime(&now)->tm_min,
                       localtime(&now)->tm_sec);
                for (j = (shmptr->tail - 1 >= shmptr->head) ? (shmptr->tail - 1) : (shmptr->tail - 1 + BUFFER_NUM); !(shmptr->is_empty) && j >= shmptr->head; j--)
                //输出缓冲区状态
                {
                    printf("%c", shmptr->letter[j % BUFFER_NUM]);
                }
                printf("\tProducer %d puts a product named as '%c'.\n",
                       num_p, pn);   //输出动作序列
                fflush(stdout);      //清除文件缓存区
                v(sem_id, SEM_FULL); //释放对文件的使用权，V(full)
            }
            shmdt(shmptr); //将共享段与进程之间解除链接
            exit(0);       //子进程终止
        }
    }

    for (; num_c < COSTOMER; num_c++) //循环创建COSTOMER个生产者
    {
        if ((pid_c = fork()) < 0) //创建一个子进程
        {
            printf("Error on fork.\n");
            exit(1); //失败退出
        }
        //如果是子进程，开始创建消费者
        if (pid_c == 0)
        {
            if ((shmptr = shmat(shm_id, 0, 0)) == (void *)-1) //将共享段与本进程相连
            {
                printf("Error on shmat.\n");
                exit(1); //失败退出
            }
            for (i = 0; i < READ_NUM; i++) //循环读READ_NUM次
            {
                p(sem_id, SEM_FULL);               // p操作，p(full),实现同步
                sleep(random_num());               //随机等待一段时间
                pn = shmptr->letter[shmptr->head]; //得到读取的产品标示字符
                shmptr->head = (shmptr->head + 1) % BUFFER_NUM;
                shmptr->is_empty = (shmptr->head == shmptr->tail);
                //更新缓冲区产品状态
                now = time(NULL); //得到系统时间
                printf("current time: %02d:%02d:%02d\t",
                       localtime(&now)->tm_hour, localtime(&now)->tm_min,
                       localtime(&now)->tm_sec);
                for (j = (shmptr->tail - 1 >= shmptr->head) ? (shmptr->tail - 1) : (shmptr->tail - 1 + BUFFER_NUM); !(shmptr->is_empty) && j >= shmptr->head; j--) // 输出缓冲区状态
                {
                    printf("%c", shmptr->letter[j % BUFFER_NUM]);
                }
                printf("\tConsumer %d gets a product named as '%c'.\n",
                       num_c, pn);
                fflush(stdout); //清除文件缓存区
                v(sem_id, SEM_EMPTY);
            }
            shmdt(shmptr); //解除共享段与本进程的连接
            exit(0);
        }
    }

    //主控程序最后退出
    while (wait(0) != -1)
        ;                        //等待子进程结束
    shmdt(shmptr);               //结束父进程和共享段的连接
    shmctl(shm_id, IPC_RMID, 0); //删除共享内存段
    semctl(sem_id, IPC_RMID, 0);
    printf("Main process ends.\n");
    fflush(stdout); //清除文件缓存区
    exit(0);
}
