//FormAI DATASET v1.0 Category: Task Scheduler ; Style: systematic
/*
  A unique C Task Scheduler program that schedules and runs tasks at specific times. This program uses an array to hold all the tasks and a priority queue to ensure the tasks are run in the order of their scheduled times. Each task is a function pointer and accepts a void pointer argument.

  Author: John Doe
  License: MIT License
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define MAX_TASKS 10

// Struct to represent a task
typedef struct task {
  void (*function)(void *arg);
  void *arg;
  time_t scheduled_time;
} task_t;

// Priority queue implementation using binary heap
typedef struct priority_queue {
  task_t *tasks[MAX_TASKS];
  int size;
} priority_queue_t;

// function prototypes
void enqueue(priority_queue_t *queue, task_t *task);
task_t *dequeue(priority_queue_t *queue);
void schedule_task(priority_queue_t *queue, task_t *task);
void run_task(task_t *task);
void dummy_function(void *arg);

int main() {
  priority_queue_t queue;
  queue.size = 0;

  // delay for a few seconds before executing a task
  task_t *task1 = malloc(sizeof(task_t));
  task1->function = dummy_function;
  task1->arg = NULL;
  task1->scheduled_time = time(NULL) + 5;  // schedule after 5 seconds
  schedule_task(&queue, task1);

  // task that takes an argument
  int arg = 10;
  task_t *task2 = malloc(sizeof(task_t));
  task2->function = dummy_function;
  task2->arg = &arg;
  task2->scheduled_time = time(NULL) + 3;  // schedule after 3 seconds
  schedule_task(&queue, task2);

  // task with earlier scheduled time than task1 and task2
  task_t *task3 = malloc(sizeof(task_t));
  task3->function = dummy_function;
  task3->arg = NULL;
  task3->scheduled_time = time(NULL) + 2;  // schedule after 2 seconds
  schedule_task(&queue, task3);

  // run tasks in order of scheduled time
  while (queue.size > 0) {
    task_t *task = dequeue(&queue);
    run_task(task);
    free(task);
  }

  return 0;
}

// enqueue a task onto the priority queue
void enqueue(priority_queue_t *queue, task_t *task) {
  if (queue->size < MAX_TASKS) {
    queue->tasks[queue->size++] = task;

    // bubble up the inserted task
    int i = queue->size - 1;
    while (i > 0 && queue->tasks[(i - 1) / 2]->scheduled_time > task->scheduled_time) {
      queue->tasks[i] = queue->tasks[(i - 1) / 2];
      i = (i - 1) / 2;
    }
    queue->tasks[i] = task;
  }
}

// dequeue the highest priority task from the priority queue
task_t *dequeue(priority_queue_t *queue) {
  if (queue->size > 0) {
    task_t *task = queue->tasks[0];
    queue->tasks[0] = queue->tasks[--queue->size];

    // bubble down the root task
    int i = 0;
    while (2 * i + 1 < queue->size) {
      int j = 2 * i + 1;
      if (j + 1 < queue->size && queue->tasks[j + 1]->scheduled_time < queue->tasks[j]->scheduled_time) {
        j++;
      }
      if (queue->tasks[i]->scheduled_time <= queue->tasks[j]->scheduled_time) {
        break;
      }
      task_t *temp = queue->tasks[i];
      queue->tasks[i] = queue->tasks[j];
      queue->tasks[j] = temp;
      i = j;
    }
    return task;
  }
  return NULL;
}

// schedule a task at its scheduled time onto the priority queue
void schedule_task(priority_queue_t *queue, task_t *task) {
  enqueue(queue, task);
}

// run a task and print the scheduled time
void run_task(task_t *task) {
  time_t current_time = time(NULL);
  printf("Executing task scheduled at %s", ctime(&task->scheduled_time));
  task->function(task->arg);
  printf("Task executed in %ld seconds\n", time(NULL) - current_time);
}

// dummy function to be executed by tasks
void dummy_function(void *arg) {
  if (arg != NULL) {
    printf("Argument passed to task is %d\n", *(int *)arg);
  }
  // simulate task execution by delaying for 1 second
  sleep(1);
}

