#include "osal/thread.h"


#ifdef _USE_HW_THREAD
#include "cli.h"


#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);


typedef struct thread_t_
{
  char        name[configMAX_TASK_NAME_LEN];
  void       (*main)(void *arg);
  void        *arg;
  bool        is_begin;
  uint32_t    priority;
  uint32_t    stack_bytes;
  TaskHandle_t handle;
} thread_t;

typedef struct
{
  int32_t  count;
  thread_t thread[THREAD_MAX_CNT];
} thread_info_t;


#define CPU_USAGE_WINDOW_MS   1000


static void cliThread(cli_args_t *args);
static void cliShowCpuUsage(void);

static bool is_begin = false;

static SemaphoreHandle_t mutex_lock;
static thread_info_t info;




bool threadInit(void)
{
  memset(&info, 0, sizeof(thread_info_t));

  mutex_lock = xSemaphoreCreateMutex();
  if (mutex_lock == NULL)
  {
    return false;
  }

  logPrintf("[OK] threadInit()\n");

  cliAdd("thread", cliThread);
  return true;
}

bool threadCreate(const char *name, void (*func)(void *arg), void *arg,
                  uint32_t priority, uint32_t stack_bytes)
{
  bool ret = false;
  uint32_t index;

  assert(info.count < THREAD_MAX_CNT);

  lock();
  if (info.count < THREAD_MAX_CNT)
  {
    index = info.count;

    strncpy(info.thread[index].name, name, sizeof(info.thread[index].name) - 1);
    info.thread[index].main        = func;
    info.thread[index].arg         = arg;
    info.thread[index].priority    = priority;
    info.thread[index].stack_bytes = stack_bytes;

    info.count = index + 1;
    ret = true;
  }
  unLock();

  return ret;
}

bool threadBegin(void)
{
  bool ret = true;


  lock();
  logPrintf("[  ] threadBegin()\n");
  logPrintf("       count : %d\n", (int)info.count);

  for (int i = 0; i < info.count; i++)
  {
    if (info.thread[i].main != NULL && info.thread[i].is_begin == false)
    {
      BaseType_t err;

      err = xTaskCreate(info.thread[i].main,
                        info.thread[i].name,
                        info.thread[i].stack_bytes / sizeof(StackType_t),
                        info.thread[i].arg,
                        info.thread[i].priority,
                        &info.thread[i].handle);

      if (err == pdPASS)
      {
        info.thread[i].is_begin = true;
        logPrintf("       OK   - %s\n", info.thread[i].name);
      }
      else
      {
        logPrintf("       Fail - %s\n", info.thread[i].name);
        ret = false;
      }
    }
  }

  logPrintf("       Free Heap : %d bytes / %d bytes\n",
            (int)xPortGetFreeHeapSize(), (int)configTOTAL_HEAP_SIZE);
  unLock();

  is_begin = true;

  return ret;
}


void cliThread(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    thread_t *p_thread;
    TaskStatus_t task_status;

    cliPrintf("is_begin  : %s\n", is_begin ? "True" : "False");
    cliPrintf("count     : %d\n", (int)info.count);
    cliPrintf("free heap : %d / %d bytes\n",
              (int)xPortGetFreeHeapSize(), (int)configTOTAL_HEAP_SIZE);

    p_thread = info.thread;
    for (int i = 0; i < info.count; i++)
    {
      if (p_thread[i].handle == NULL)
      {
        cliPrintf("%-16s, (not started)\n", p_thread[i].name);
        continue;
      }

      vTaskGetInfo(p_thread[i].handle, &task_status, pdTRUE, eInvalid);

      cliPrintf("%-16s, stack : %4d free %04d, prio : %d\n",
                p_thread[i].name,
                (int)p_thread[i].stack_bytes,
                (int)task_status.usStackHighWaterMark * (int)sizeof(StackType_t),
                (int)p_thread[i].priority);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "task"))
  {
    const size_t bytes_per_task = 48;
    char *list_buf;

    list_buf = (char *)pvPortMalloc(uxTaskGetNumberOfTasks() * bytes_per_task);
    if (list_buf != NULL)
    {
      vTaskList(list_buf);
      cliPrintf("Task Name\tState\tPrio\tStack\tNum#\n");
      cliWrite((uint8_t *)list_buf, strlen(list_buf));
      vPortFree(list_buf);

      cliPrintf("Free Heap : %d bytes\n", (int)xPortGetFreeHeapSize());
    }
    else
    {
      cliPrintf("pvPortMalloc fail\n");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "cpu"))
  {
    cliShowCpuUsage();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("thread info\n");
    cliPrintf("thread task\n");
    cliPrintf("thread cpu\n");
  }
}

/*
 * 두 스냅샷의 런타임 카운터 차이로 구간 점유율을 낸다.
 * vTaskGetRunTimeStats() 는 부팅 이후 누적이라 방금 무엇이 CPU 를 먹는지 알 수 없다.
 */
void cliShowCpuUsage(void)
{
  TaskStatus_t *p_pre;
  TaskStatus_t *p_cur;
  uint32_t pre_cnt;
  uint32_t cur_cnt;
  uint32_t task_cnt;
  uint32_t total;

  task_cnt = uxTaskGetNumberOfTasks();

  p_pre = (TaskStatus_t *)pvPortMalloc(task_cnt * sizeof(TaskStatus_t));
  p_cur = (TaskStatus_t *)pvPortMalloc(task_cnt * sizeof(TaskStatus_t));
  if (p_pre == NULL || p_cur == NULL)
  {
    cliPrintf("pvPortMalloc fail\n");
    vPortFree(p_pre);
    vPortFree(p_cur);
    return;
  }

  task_cnt = uxTaskGetSystemState(p_pre, task_cnt, &pre_cnt);
  delay(CPU_USAGE_WINDOW_MS);
  uxTaskGetSystemState(p_cur, task_cnt, &cur_cnt);

  total = cur_cnt - pre_cnt;
  if (total == 0)
  {
    cliPrintf("run time counter not running\n");
  }
  else
  {
    cliPrintf("%-16s %6s %5s\n", "Task Name", "Prio", "CPU");
    for (uint32_t i = 0; i < task_cnt; i++)
    {
      uint32_t used = 0;

      for (uint32_t j = 0; j < task_cnt; j++)
      {
        if (p_cur[j].xTaskNumber == p_pre[i].xTaskNumber)
        {
          used = p_cur[j].ulRunTimeCounter - p_pre[i].ulRunTimeCounter;
          break;
        }
      }

      cliPrintf("%-16s %6d %4d.%d %%\n",
                p_pre[i].pcTaskName,
                (int)p_pre[i].uxCurrentPriority,
                (int)(used * 100 / total),
                (int)(used * 1000 / total % 10));
    }
  }

  vPortFree(p_pre);
  vPortFree(p_cur);
}


#endif
