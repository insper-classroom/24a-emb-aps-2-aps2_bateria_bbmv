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

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "mpu6050.h"
#include <math.h>
#include <Fusion.h>

// CONSTANTES E PORTAS

#define SAMPLE_PERIOD (0.01f)
const int MPU_ADDRESS = 0x68;
const int I2C_SDA_GPIO = 4;
const int I2C_SCL_GPIO = 5;
const int ADC_X = 27;
const int ADC_Y = 26;
const uint BTN_PODER = 11;
const uint BTN_ENTER = 12;

// Implementação do filtro de média móvel
#define WINDOW_SIZE 5

// FILAS
QueueHandle_t xQueueAdc;
QueueHandle_t xQueueBTN;

// STRUCTS DAS INFOS A SEREM PASSADAS
typedef struct adc {
    int axis;
    int val;
} adc_t;


// Estrutura para a média móvel
typedef struct {
    int window[WINDOW_SIZE];
    int window_index;
} MovingAverage;

// Função para inicializar a estrutura da média móvel
void init_moving_average(MovingAverage *ma) {
    for (int i = 0; i < WINDOW_SIZE; i++) {
        ma->window[i] = 0;
    }
    ma->window_index = 0;
}

// Função para calcular a média móvel
int moving_average(MovingAverage *ma, int new_value) {
    // Adiciona o novo valor ao vetor de janela
    ma->window[ma->window_index] = new_value;

    // Atualiza o índice da janela circularmente
    ma->window_index = (ma->window_index + 1) % WINDOW_SIZE;

    // Calcula a média dos valores na janela
    int sum = 0;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        sum += ma->window[i];
    }
    return sum / WINDOW_SIZE;
}
// função de escala do joystick
int scaled_value(int raw_value) {
    int scaled_value;
    raw_value -= 2048; // Center the value
    raw_value /= 8; // Scale the value

    if (raw_value < 150 && raw_value > -150) {
        return 0; // Dead zone ---- fica entre x 145 e 1 130
    } else {
        return raw_value;
    }
}

// FUNÇÕES PARA ENVIO DE DADOS NA UART 
void write_package(adc_t data) {
    int val = data.val;
    int msb = val >> 8;
    int lsb = val & 0xFF ;

    uart_putc_raw(uart0, data.axis);
    uart_putc_raw(uart0, lsb);
    uart_putc_raw(uart0, msb);
    uart_putc_raw(uart0, -1);
}

void uart_task(void *p) {
    adc_t data;

    while (1) {
        if(xQueueReceive(xQueueAdc, &data, portMAX_DELAY)){
            write_package(data);
        }
    }
}

// FUNÇÕES PARA LEITURA DO MPU6050
static void mpu6050_reset() {
    // Two byte reset. First byte register, second byte data
    // There are a load more options to set up the device in different ways that could be added here
    uint8_t buf[] = {0x6B, 0x00};
    i2c_write_blocking(i2c_default, MPU_ADDRESS, buf, 2, false);
}

static void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], int16_t *temp) {
    // For this particular device, we send the device the register we want to read
    // first, then subsequently read from the device. The register is auto incrementing
    // so we don't need to keep sending the register we want, just the first.

    uint8_t buffer[6];

    // Start reading acceleration registers from register 0x3B for 6 bytes
    uint8_t val = 0x3B;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true); // true to keep master control of bus
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 6, false);

    for (int i = 0; i < 3; i++) {
        accel[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
    }

    // Now gyro data from reg 0x43 for 6 bytes
    // The register is auto incrementing on each read
    val = 0x43;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 6, false);  // False - finished with bus

    for (int i = 0; i < 3; i++) {
        gyro[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);;
    }

    // Now temperature from reg 0x41 for 2 bytes
    // The register is auto incrementing on each read
    val = 0x41;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 2, false);  // False - finished with bus

    *temp = buffer[0] << 8 | buffer[1];
}

void mpu6050_task(void *p) {

    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(I2C_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_GPIO);
    gpio_pull_up(I2C_SCL_GPIO);

    mpu6050_reset();
    int16_t acceleration[3], gyro[3], temp;

    FusionAhrs ahrs;
    FusionAhrsInitialise(&ahrs);

    while(1) {
        mpu6050_read_raw(acceleration, gyro, &temp);

        FusionVector gyroscope = {
            .axis.x = gyro[0] / 131.0f, // Conversão para graus/s
            .axis.y = gyro[1] / 131.0f,
            .axis.z = gyro[2] / 131.0f,
        };

        FusionVector accelerometer = {
            .axis.x = acceleration[0] / 16384.0f, // Conversão para g
            .axis.y = acceleration[1] / 16384.0f,
            .axis.z = acceleration[2] / 16384.0f,
        };      
        FusionAhrsUpdateNoMagnetometer(&ahrs, gyroscope, accelerometer, SAMPLE_PERIOD);

        const FusionEuler euler = FusionQuaternionToEuler(FusionAhrsGetQuaternion(&ahrs));

        // enviar para fila a struct do x

        // struct adc x_data = {0, (int)euler.angle.roll};
        // //printf("dado x")
        // //xQueueSend(xQueueAdc, &x_data, portMAX_DELAY);

        // // enviar para fila a struct do y
        // struct adc y_data = {1, (int)euler.angle.pitch};
        // //xQueueSend(xQueueAdc, &y_data, portMAX_DELAY);
        // vTaskDelay(pdMS_TO_TICKS(10));
        
        // roll FAZER DESSE + ESCALA + MEDIA MOVEL


        //printf("Roll %0.1f, Pitch %0.1f, Yaw %0.1f\n", euler.angle.roll, euler.angle.pitch, euler.angle.yaw);
        //printf("Temp. = %f\n", (temp / 340.0) + 36.53);
        // printf("Acc. X = %d, Y = %d, Z = %d\n", acceleration[0], acceleration[1], acceleration[2]);
        // printf("Gyro. X = %d, Y = %d, Z = %d\n", gyro[0], gyro[1], gyro[2]);
        //printf("Temp. = %f\n", (temp / 340.0) + 36.53);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// TASK JOYSTICK
void y_task(void *p) {

    adc_init();
    adc_gpio_init(ADC_X);

    MovingAverage ma;
    init_moving_average(&ma);
   
    // Make sure GPIO is high-impedance, no pullups etc
    
    while (1) {
         
        // Select ADC input 1 (GPIO27)
         vTaskDelay(1);
        adc_select_input(1);

        int y = moving_average(&ma, adc_read());
        y = scaled_value(y); 
        //int x_read = adc_read();
        if (y > 0) {
           struct adc y_data = {103,1};
            xQueueSend(xQueueAdc, &y_data, portMAX_DELAY);
        } else if (y > 0){
            struct adc y_data2 = {108,1};
            xQueueSend(xQueueAdc, &y_data2, portMAX_DELAY);
        } else {
            struct adc y_data = {103,0};
            xQueueSend(xQueueAdc, &y_data, portMAX_DELAY);
            struct adc y_data2 = {108,0};
            xQueueSend(xQueueAdc, &y_data2, portMAX_DELAY);  
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void hc06_task(void *p) {
    uart_init(HC06_UART_ID, HC06_BAUD_RATE);
    gpio_set_function(HC06_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(HC06_RX_PIN, GPIO_FUNC_UART);
    hc06_init("BRABAS", "0000");

    while (true) {
        uart_puts(HC06_UART_ID, "OLAAA ");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}



//BTN CALLBACK
void btn_callback(uint gpio, uint32_t events) {
    if (gpio == BTN_ENTER) {
        if (events == GPIO_IRQ_EDGE_FALL) {
            struct adc btn_data = {28, 1};
            xQueueSend(xQueueAdc, &btn_data, portMAX_DELAY);
        } else if (events == GPIO_IRQ_EDGE_RISE) {
            struct adc btn_data = {28, 0};
            xQueueSend(xQueueAdc, &btn_data, portMAX_DELAY);
        }
    } else if (gpio == BTN_PODER) {
        if (events == GPIO_IRQ_EDGE_FALL) {
            struct adc btn_data = {57, 1};
            xQueueSend(xQueueAdc, &btn_data, portMAX_DELAY);
        } else if (events == GPIO_IRQ_EDGE_RISE) {  
            struct adc btn_data = {57, 0};
            xQueueSend(xQueueAdc, &btn_data, portMAX_DELAY);
        }
    }
}


void btn_init(void){

    gpio_init(BTN_ENTER);
    gpio_set_dir(BTN_ENTER, GPIO_IN);
    gpio_pull_up(BTN_ENTER);
    gpio_set_irq_enabled_with_callback(BTN_ENTER, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &btn_callback);

    gpio_init(BTN_PODER);
    gpio_set_dir(BTN_PODER, GPIO_IN);
    gpio_pull_up(BTN_PODER);
    gpio_set_irq_enabled(BTN_PODER, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
}

int main() {
    stdio_init_all();
    btn_init(); // inicializa os botões

    // criando a fila das informações de comando 
    xQueueAdc = xQueueCreate(32, sizeof(adc_t));

    // printf("Start bluetooth task\n");
    xTaskCreate(hc06_task, "UART_Task 1", 4096, NULL, 1, NULL);

    //xTaskCreate(y_task, "y_task", 256, NULL, 1, NULL);
    xTaskCreate(y_task, "x_task", 256, NULL, 1, NULL);
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 1, NULL);
    //xTaskCreate(mpu6050_task, "mpu6050_Task 1", 8192, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
