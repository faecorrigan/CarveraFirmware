#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

extern uint32_t SystemCoreClock;

#define configUSE_NEWLIB_REENTRANT      1
#define configUSE_PREEMPTION            1
#define configUSE_IDLE_HOOK	            0
#define configUSE_TICK_HOOK             0
#define configUSE_TIMERS                1
#define configUSE_COUNTING_SEMAPHORES   1
#define configTIMER_TASK_STACK_DEPTH    configMINIMAL_STACK_SIZE
#define configTIMER_QUEUE_LENGTH        16
#define configCPU_CLOCK_HZ              ( ( unsigned long ) SystemCoreClock )
#define configTICK_RATE_HZ              ( ( portTickType ) 1000 )
#define configMINIMAL_STACK_SIZE        ( ( unsigned short ) 80 )
#define configMAX_TASK_NAME_LEN	        ( 12 )
#define configUSE_TRACE_FACILITY        1 /* This is set to one so each task is given a unique number, which is then used to generate the logic analyzer output. */
#define configUSE_16_BIT_TICKS          0
#define configIDLE_SHOULD_YIELD         0
#define configUSE_CO_ROUTINES           0
#define configUSE_MUTEXES               0
#define configUSE_RECURSIVE_MUTEXES     0
#define configCHECK_FOR_STACK_OVERFLOW  1

#define configMAX_PRIORITIES			( 5 )
#define configMAX_CO_ROUTINE_PRIORITIES ( 2 )
#define configQUEUE_REGISTRY_SIZE		0

#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           0
#define INCLUDE_vTaskDelete	                1
#define INCLUDE_vTaskCleanUpResources       0
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_uxTaskGetStackHighWaterMark	0

#define configKERNEL_INTERRUPT_PRIORITY 		( 7 << 5 )	/* Priority 7, or 255 as only the top three bits are implemented.  This is the lowest priority. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	( 5 << 5 )  /* Priority 5, or 160 as only the top three bits are implemented. */

/*
 * Use the Cortex-M3 optimisations, rather than the generic C implementation.
 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1

#endif /* FREERTOS_CONFIG_H */