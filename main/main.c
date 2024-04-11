/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include <string.h>

#include "pico/stdlib.h"
#include <stdio.h>

#include "hc06.h"

const uint BTN_SPACE = 16;

QueueHandle_t xQueueBTN;

struc

void hc06_task(void *p) {
    uart_init(HC06_UART_ID, HC06_BAUD_RATE);
    gpio_set_function(HC06_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(HC06_RX_PIN, GPIO_FUNC_UART);
    hc06_init("aps2_legal", "1234");

    while (true) {
        uart_puts(HC06_UART_ID, "OLAAA ");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}



// BTN CALLBACK
void btn_callback(uint gpio, uint32_t events) {
    if (gpio == BTN_SPACE) {
        if (events == 0x8) {
            printf("Button SPACE pressed (rising edge)\n");
            xQueueSendFromISR(xQueueBTN, &BTN_1_OLED, 0);
        } else if (events == 0x4) {
            printf("Button SPACE released (falling edge)\n");
        }
    }
}

void btn_init(void){

    gpio_init(BTN_SPACE);
    gpio_set_dir(BTN_SPACE, GPIO_IN);
    gpio_pull_up(BTN_SPACE);
    gpio_set_irq_enabled_with_callback(BTN_SPACE, GPIO_IRQ_EDGE_RISE_FALL, true, &btn_callback);
}

int main() {
    stdio_init_all();

    printf("Start bluetooth task\n");

    xTaskCreate(hc06_task, "UART_Task 1", 4096, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
