#include <mr-base/mr_autoinit.h>
#include <mr-task/mr_task.h>
#include <stdio.h>

void thread1(void *args) {
    MR_UNUSED(args);

    for (size_t i = 0; i < 10; i++) {
        printf("thread-1 %llu\r\n", i);
        mr_task_yield();
    }
}

void thread2(void *args) {
    MR_UNUSED(args);

    for (size_t i = 0; i < 10; i++) {
        printf("thread-2 %llu\r\n", i);
        mr_task_yield();
    }
}

int main(void) {
    mr_autoinit();

    mr_task_t *task1 = mr_task_create("th1", thread1, MR_NULL, 102400, 2);
    mr_task_t *task2 = mr_task_create("th2", thread2, MR_NULL, 102400, 2);
    mr_task_startup(task1);
    mr_task_startup(task2);
    return 0;
}
