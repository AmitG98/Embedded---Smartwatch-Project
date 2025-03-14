#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "System/system.h"
#include "System/delay.h"
#include "oledDriver/oledC.h"
#include "oledDriver/oledC_colors.h"
#include "oledDriver/oledC_shapes.h"
#include "Accel_i2c.h"
#include <libpic30.h>
#include <xc.h>

// ---------------- Button Pin Defines ----------------
// Adjust these to match your hardware (active-low assumed)
#define S1_PORT PORTAbits.RA0
#define S2_PORT PORTAbits.RA1
#define S1_TRIS TRISAbits.TRISA0
#define S2_TRIS TRISAbits.TRISA1

// ---------------- Defines ----------------
#define WRITE_ADDRESS 0x3A
#define REG_POWER_CTL 0x2D
#define REG_DATAX0 0x32
#define REG_DATAY0 0x34
#define REG_DATAZ0 0x36
#define MEASURE_MODE 0x08
#define HISTORY_SIZE 60
#define STEP_THRESHOLD 500.0f

typedef struct
{
    int16_t x, y, z;
} ACCEL_DATA_t;

// ---------------- Globals for Pedometer & Clock ----------------
static bool wasAboveThreshold = false;
static bool movementDetected = false;
static uint16_t stepCount = 0;
static uint8_t inactivityCounter = 0;
const float baselineGravity = 1024.0f;
static uint8_t stepsHistory[HISTORY_SIZE] = {0};
static uint8_t currentSecondIndex = 0;
// For smoothing the displayed pace
static float displayedPace = 0.0f;
// Global seconds counter (updated every Timer1 interrupt)
static uint32_t globalSeconds = 0;
bool is12HourFormat = false;      // Tracks if we are in 12H mode (true) or 24H (false)
bool inTimeFormatSubpage = false; // Are we currently in the “12H/24H” sub-page?
// For the sub-page selection: 0 => “12H”, 1 => “24H”
uint8_t timeFormatSelectedIndex = 0;
static bool footToggle = false;
bool forceClockRedraw = false;
typedef struct
{
    uint8_t hours, minutes, seconds, day, month;
} ClockTime;
static const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
ClockTime currentTime = {8, 24, 35, 24, 1};
bool inMenu = false;

// ---------------- Globals for Graph ----------------
#define GRAPH_HISTORY_SIZE 90  // Store last 90 seconds of step rate
volatile uint8_t stepRateHistory[GRAPH_HISTORY_SIZE] = {0};
static uint8_t graphIndex = 0;  // Index to track the current second
bool inGraphMode = false; // Are we currently in the “Graph” sub-page?

// ---------------- Function declaration to avoid implicit warnings ----------------
void updateMenuClock(void);
void drawTimeFormatSubpage(void);

// ---------------- Foot Bitmaps (16×16) ----------------
static const uint16_t foot1Bitmap[16] = {
    0x7800, 0xF800, 0xFC00, 0xFC00,
    0xFC00, 0x7C1E, 0x783E, 0x047F,
    0x3F9F, 0x1F3E, 0x0C3E, 0x003E,
    0x0004, 0x00F0, 0x01F0, 0x00E0};
static const uint16_t foot2Bitmap[16] = {
    0x001E, 0x003F, 0x003F, 0x007F,
    0x003F, 0x383E, 0x7C1E, 0x7E10,
    0x7E7C, 0x7E78, 0x7C30, 0x3C00,
    0x2000, 0x1E00, 0x1F00, 0x0E00};

// ---------------- Functions for Graph ----------------

void drawMenu(void);
#define GRAPH_WIDTH 90
#define GRAPH_HEIGHT 90
// Draw Step Rate Graph on OLED
void drawStepRateGraph(void) {
    inGraphMode = true;  // Enter graph mode
    oledC_clearScreen();  // Clear screen before drawing

    // Step rate history: 90 seconds of fake step data (1 value per second)
    int step_rate_history[90];

    // Start with an initial step rate (you can adjust this)
    int current_step_rate = 50;  // Start with 50 steps per minute

    // Fill the step_rate_history with more realistic data
    for (int i = 0; i < 90; i++) {
        // Simulate a gradual increase or decrease in step rate
        int change = (rand() % 11) - 5;  // Random change between -5 and +5 steps
        current_step_rate += change;

        // Make sure the step rate stays within 0-100 steps per minute
        if (current_step_rate < 0) current_step_rate = 0;
        if (current_step_rate > 100) current_step_rate = 100;

        // Store the current step rate for this second
        step_rate_history[i] = current_step_rate;
    }

    // Drawing the X axis (time axis)
    // oledC_DrawLine(10, GRAPH_HEIGHT - 10, GRAPH_WIDTH - 10, GRAPH_HEIGHT - 10, 1, 0xFFFF); // Horizontal line for X-axis

    // Draw horizontal grid lines at 30, 60, and 100 steps
    int step_values[] = {0, 30, 60, 100};  // Y-axis step values
    int min_y = GRAPH_HEIGHT - 10;  // Y position for 0 (bottom of the graph)
    int max_y = 10;  // Y position for 100 (top of the graph)

    // Add labels to Y axis (step rate)
    for (int i = 1; i < 4; i++) {
        // int y_pos = GRAPH_HEIGHT - 10 - (step_values[i] * (GRAPH_HEIGHT - 20) / 100);
        int y_pos = min_y - ((step_values[i] * (min_y - max_y)) / 100);
        char label[4];
        sprintf(label, "%d", step_values[i]); // Labels 0, 30, 60, 100
        oledC_DrawString(0, y_pos - 4, 1, 1, label, 0xFFFF); // Draw label for each grid line

        // Draw horizontal dotted grid line at this Y-position
        for (int j = 0; j < 30; j++) {  // 30 dots for the full X-axis
            int x_pos = 20 + (j * (GRAPH_WIDTH - 20) / 29);  // 30 dots over the 90 seconds (29 intervals)
            oledC_DrawPoint(x_pos, y_pos, 0xFFFF);  // Draw a small dot on the grid line
        }
    }

    // Draw X-axis as small dots for 90 seconds
    for (int i = 0; i <= 9; i++) {
        int x_pos = 20 + (i * (GRAPH_WIDTH - 20) / 9); // Divide by 9 to get 10 evenly spaced dots for 90 seconds
        oledC_DrawThickPoint(x_pos, GRAPH_HEIGHT - 8, 2, 0xFFFF);
    }

    // Plot the fake step rate history
    for (int i = 0; i < 90; i++) {
        // Calculate the X position based on the second (i represents the second)
        int x_pos = 20 + (i * (GRAPH_WIDTH - 20) / 89);  // 90 seconds, 89 intervals

        // Map the step rate value (0 to 100) to the Y-axis (invert the Y calculation)
        int y_pos = min_y - (step_rate_history[i] * (min_y - max_y) / 100);

        // Plot the step rate history as a point
        oledC_DrawPoint(x_pos, y_pos, 0xFFFF);  // Draw a point for each second
    }

    bool s1WasPressed = false;
    bool s2WasPressed = false;

    while (inGraphMode)
    {
        bool s1State = (PORTAbits.RA11 == 0);  // Check S1 button
        bool s2State = (PORTAbits.RA12 == 0);  // Check S2 button

        // If both S1 and S2 are pressed, exit the graph
        if (s1State && s2State)
        {
            inGraphMode = false;  // Exit graph mode
            inMenu = true;  // Return to menu mode
            drawMenu();
            break;
        }

        s1WasPressed = s1State;
        s2WasPressed = s2State;

        DELAY_milliseconds(100);  // Prevent button bouncing
    }
}

// ---------------- Functions for Pedometer, Clock, etc. ----------------
void errorStop(char *msg)
{
    oledC_DrawString(0, 20, 1, 1, (uint8_t *)msg, OLEDC_COLOR_DARKRED);
    printf("Error: %s\n", msg);
    for (;;)
        ;
}

int16_t readAxis(uint8_t regAddress)
{
    uint8_t lowByte, highByte;
    int retries = 3;
    for (int i = 0; i < retries; i++)
    {
        if (i2cReadSlaveRegister(WRITE_ADDRESS, regAddress, &lowByte) == OK)
            break;
        if (i == retries - 1)
            errorStop("I2C Read Error (LSB)");
        DELAY_milliseconds(10);
    }
    for (int i = 0; i < retries; i++)
    {
        if (i2cReadSlaveRegister(WRITE_ADDRESS, regAddress + 1, &highByte) == OK)
            break;
        if (i == retries - 1)
            errorStop("I2C Read Error (MSB)");
        DELAY_milliseconds(10);
    }
    return ((int16_t)highByte << 8) | lowByte;
}

void initAccelerometer(void)
{
    I2Cerror err;
    uint8_t deviceId = 0;
    for (int i = 0; i < 3; i++)
    {
        err = i2cReadSlaveRegister(WRITE_ADDRESS, 0x00, &deviceId);
        if (err == OK && deviceId == 0xE5)
            break;
        if (i == 2)
            errorStop("I2C Error or Wrong Device ID");
        DELAY_milliseconds(10);
    }
    for (int i = 0; i < 3; i++)
    {
        err = i2cWriteSlave(WRITE_ADDRESS, REG_POWER_CTL, MEASURE_MODE);
        if (err == OK)
            break;
        if (i == 2)
            errorStop("Accel Power Error");
        DELAY_milliseconds(10);
    }
    for (int i = 0; i < 3; i++)
    {
        err = i2cWriteSlave(WRITE_ADDRESS, 0x31, 0x0B);
        if (err == OK)
            break;
        if (i == 2)
            errorStop("Accel Data Format Error");
        DELAY_milliseconds(10);
    }
}

void detectStep(void)
{
    ACCEL_DATA_t accel;
    accel.x = readAxis(REG_DATAX0);
    accel.y = readAxis(REG_DATAY0);
    accel.z = readAxis(REG_DATAZ0);

    float ax = accel.x * 4.0f;
    float ay = accel.y * 4.0f;
    float az = accel.z * 4.0f;
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    float dynamic = fabsf(mag - baselineGravity);
    bool above = (dynamic > STEP_THRESHOLD);
    movementDetected = above;

    if (above && !wasAboveThreshold)
    {
        stepCount++;
        stepsHistory[currentSecondIndex]++;
        printf("Step detected! Count=%u\n", stepCount);
    }
    wasAboveThreshold = above;
}

void drawSteps(void)
{
    uint16_t sum = 0;
    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        sum += stepsHistory[i];
    }
    uint16_t rawPace = sum;

    static uint32_t lastUpdateSecond = 0;
    if (globalSeconds != lastUpdateSecond)
    {
        if (movementDetected)
        {
            if (rawPace > displayedPace)
            {
                displayedPace += 1.0f;
                if (displayedPace > rawPace)
                    displayedPace = rawPace;
            }
            else if (rawPace < displayedPace)
            {
                displayedPace -= 1.0f;
                if (displayedPace < rawPace)
                    displayedPace = rawPace;
            }
        }
        else
        {
            if (inactivityCounter >= 1)
            {
                displayedPace -= 1.0f;
                if (displayedPace < 0)
                    displayedPace = 0.0f;
            }
        }
        lastUpdateSecond = globalSeconds;
    }

    uint16_t pace = (uint16_t)(displayedPace + 0.5f);

    static char oldStr[6] = "";
    char newStr[6];
    if (pace == 0)
    {
        if (oldStr[0] != '\0')
        {
            oledC_DrawString(80, 2, 1, 1, (uint8_t *)oldStr, OLEDC_COLOR_BLACK);
            oldStr[0] = '\0';
        }
        return;
    }
    sprintf(newStr, "%u", pace);
    if (strcmp(oldStr, newStr) != 0)
    {
        oledC_DrawString(80, 2, 1, 1, (uint8_t *)oldStr, OLEDC_COLOR_BLACK);
        oledC_DrawString(80, 2, 1, 1, (uint8_t *)newStr, OLEDC_COLOR_WHITE);
        strcpy(oldStr, newStr);
    }
}

static void twoDigitString(uint8_t val, char *buffer)
{
    buffer[0] = (val / 10) + '0';
    buffer[1] = (val % 10) + '0';
    buffer[2] = '\0';
}

void updateDate(ClockTime *t)
{
    t->day++;                                 // Increment the day
    uint8_t days = daysInMonth[t->month - 1]; // Get max days for current month
    if (t->day > days)
    {
        t->day = 1;
        t->month++;
        if (t->month > 12)
            t->month = 1;
    }
}

void incrementTime(ClockTime *t)
{
    t->seconds++;
    if (t->seconds >= 60)
    {
        t->seconds = 0;
        t->minutes++;
    }
    if (t->minutes >= 60)
    {
        t->minutes = 0;
        t->hours++;
    }
    if (t->hours >= 24)
    {
        t->hours = 0;
        updateDate(t);
    }
}

void drawClock(ClockTime *time)
{
    static char oldTime[9] = "";
    static char oldDate[6] = "";
    static bool oldWas12H = false;
    static bool oldPM = false;

    if (forceClockRedraw)
    {
        oldTime[0] = '\0';
        oldDate[0] = '\0';
        oldWas12H = !is12HourFormat;
        oldPM = false;
        forceClockRedraw = false;
    }

    char newTime[9];
    char buff[3];
    char newDateStr[6];
    bool pm = false;

    uint8_t displayHrs = time->hours;
    if (is12HourFormat)
    {
        if (displayHrs == 0)
        {
            displayHrs = 12; // midnight is 12 AM
        }
        else if (displayHrs >= 12)
        {
            pm = true;
            if (displayHrs > 12)
                displayHrs -= 12;
        }
    }

    twoDigitString(displayHrs, buff);
    sprintf(newTime, "%s:", buff);
    twoDigitString(time->minutes, buff);
    strcat(newTime, buff);
    strcat(newTime, ":");
    twoDigitString(time->seconds, buff);
    strcat(newTime, buff);

    // If there is any change in time or format, clear old areas and redraw.
    if (strcmp(oldTime, newTime) != 0 || oldWas12H != is12HourFormat || oldPM != pm)
    {
        oledC_DrawString(8, 45, 2, 2, (uint8_t *)oldTime, OLEDC_COLOR_BLACK);
        oledC_DrawRectangle(50, 45, 80, 60, OLEDC_COLOR_BLACK);
        oledC_DrawString(8, 45, 2, 2, (uint8_t *)newTime, OLEDC_COLOR_WHITE);
        if (is12HourFormat)
        {
            if (pm)
                oledC_DrawString(0, 85, 1, 1, (uint8_t *)"PM", OLEDC_COLOR_WHITE);
            else
                oledC_DrawString(0, 85, 1, 1, (uint8_t *)"AM", OLEDC_COLOR_WHITE);
        }

        strcpy(oldTime, newTime);
        oldWas12H = is12HourFormat;
        oldPM = pm;
    }

    twoDigitString(time->day, buff);
    sprintf(newDateStr, "%s/", buff);
    twoDigitString(time->month, buff);
    strcat(newDateStr, buff);

    if (strcmp(oldDate, newDateStr) != 0)
    {
        oledC_DrawString(65, 85, 1, 1, (uint8_t *)oldDate, OLEDC_COLOR_BLACK);
        oledC_DrawString(65, 85, 1, 1, (uint8_t *)newDateStr, OLEDC_COLOR_WHITE);
        strcpy(oldDate, newDateStr);
    }
}

void drawFootIcon(uint8_t x, uint8_t y, const uint16_t *bitmap, uint8_t width, uint8_t height)
{
    for (uint8_t row = 0; row < height; row++)
        for (uint8_t col = 0; col < width; col++)
            if (bitmap[row] & (1 << (width - 1 - col)))
                oledC_DrawPoint(x + col, y + row, OLEDC_COLOR_WHITE);
}

// ---------------- 12H/24H SYSTEM ---------------- //
// New function to handle the 12H/24H subpage logic
void handleTimeFormatSelection(void)
{
    inTimeFormatSubpage = true;
    timeFormatSelectedIndex = (is12HourFormat ? 0 : 1);

    drawTimeFormatSubpage();

    bool s1WasPressed = false;
    bool s2WasPressed = false;

    while (inTimeFormatSubpage)
    {
        bool s1State = (PORTAbits.RA11 == 0); // Confirmation button
        bool s2State = (PORTAbits.RA12 == 0); // Navigation button

        // S2 cycles the selection on a rising edge.
        if (s2State && !s2WasPressed)
        {
            timeFormatSelectedIndex = (timeFormatSelectedIndex + 1) % 2;
            drawTimeFormatSubpage();
        }

        // S1 confirms the current selection.
        if (s1State && !s1WasPressed)
        {
            is12HourFormat = (timeFormatSelectedIndex == 0); // index 0 = 12H, 1 = 24H
            inTimeFormatSubpage = false;
            break;
        }

        s1WasPressed = s1State;
        s2WasPressed = s2State;

        DELAY_milliseconds(100); // Adjust as needed.
    }
}

void drawTimeFormatSubpage(void)
{
    oledC_clearScreen();
    // Display a title (optional)
    oledC_DrawString(10, 5, 1, 1, (uint8_t *)"Choose Format:", OLEDC_COLOR_WHITE);

    // Draw "12H" item at y=25.
    if (timeFormatSelectedIndex == 0)
    {
        oledC_DrawRectangle(8, 23, 60, 35, OLEDC_COLOR_WHITE);
        oledC_DrawString(10, 25, 1, 1, (uint8_t *)"12H", OLEDC_COLOR_BLACK);
    }
    else
    {
        oledC_DrawString(10, 25, 1, 1, (uint8_t *)"12H", OLEDC_COLOR_WHITE);
    }

    // Draw "24H" item at y=40.
    if (timeFormatSelectedIndex == 1)
    {
        oledC_DrawRectangle(8, 38, 60, 50, OLEDC_COLOR_WHITE);
        oledC_DrawString(10, 40, 1, 1, (uint8_t *)"24H", OLEDC_COLOR_BLACK);
    }
    else
    {
        oledC_DrawString(10, 40, 1, 1, (uint8_t *)"24H", OLEDC_COLOR_WHITE);
    }
}

// ---------------- MENU SYSTEM (Integrated in main.c) ----------------
#define MENU_ITEMS_COUNT 5
const char *menuItems[MENU_ITEMS_COUNT] = {
    "Pedometer Graph",
    "12H/24H Interval",
    "Set Time",
    "Set Date",
    "Exit"};

volatile uint8_t selectedMenuItem = 0;

void drawMenu(void)
{
    // Clear the screen to black.
    oledC_clearScreen();

    // Draw the mini clock at the top right.
    updateMenuClock();

    // Draw menu items (no "MENU" label)
    for (uint8_t i = 0; i < MENU_ITEMS_COUNT; i++)
    {
        uint8_t yPos = 20 + (i * 12);
        if (i == selectedMenuItem)
        {
            // Draw a white highlight rectangle for the selected item.
            oledC_DrawRectangle(3, yPos - 2, 115, yPos + 10, OLEDC_COLOR_WHITE);
            // Draw black text inside the blue rectangle.
            oledC_DrawString(4, yPos, 1, 1, (uint8_t *)menuItems[i], OLEDC_COLOR_BLACK);
        }
        else
        {
            // Draw unselected items as white text on black.
            oledC_DrawString(4, yPos, 1, 1, (uint8_t *)menuItems[i], OLEDC_COLOR_WHITE);
        }
    }
}

void updateMenuClock(void)
{
    char timeStr[9], buff[3];
    // If 12H, subtract 12 if hours >= 12, etc.
    // Then append AM/PM if is12HourFormat is true.

    uint8_t displayHrs = currentTime.hours;
    bool pm = false;
    if (is12HourFormat)
    {
        if (displayHrs >= 12)
        {
            pm = true;
            if (displayHrs > 12)
                displayHrs -= 12;
        }
        else if (displayHrs == 0)
        {
            displayHrs = 12; // midnight hour = 12 AM
        }
    }

    twoDigitString(displayHrs, buff);
    strcpy(timeStr, buff);
    strcat(timeStr, ":");
    twoDigitString(currentTime.minutes, buff);
    strcat(timeStr, buff);
    strcat(timeStr, ":");
    twoDigitString(currentTime.seconds, buff);
    strcat(timeStr, buff);

    // Clear old clock
    oledC_DrawRectangle(30, 2, 115, 10, OLEDC_COLOR_BLACK);
    // Draw time
    oledC_DrawString(30, 2, 1, 1, (uint8_t *)timeStr, OLEDC_COLOR_WHITE);

    // If 12H, append AM/PM
    if (is12HourFormat)
    {
        if (pm)
            oledC_DrawString(80, 2, 1, 1, (uint8_t *)"PM", OLEDC_COLOR_WHITE);
        else
            oledC_DrawString(80, 2, 1, 1, (uint8_t *)"AM", OLEDC_COLOR_WHITE);
    }
}

// void executeMenuAction(void)
// {
//     if (selectedMenuItem == 0)
//     {
//         drawStepRateGraph();  
//         return;
//     }
//     else if (selectedMenuItem == 1)
//     {
//         handleTimeFormatSelection();
//         drawMenu();
//         return;
//     }
//     else if (selectedMenuItem == 2)
//     {
//         return;
//     }
//     else if (selectedMenuItem == 3)
//     {
//         return;
//     }
//     else if (selectedMenuItem == 4)
//     {
//         inMenu = false;
//         forceClockRedraw = true;
//         oledC_DrawRectangle(40, 2, 115, 10, OLEDC_COLOR_BLACK);
//         oledC_clearScreen();
//         return;
//     }
//     else
//     {
//         printf("Invalid Menu Option\n");
//     }
// }
void executeMenuAction(void)
{
    switch (selectedMenuItem)
    {
    case 0:
        oledC_clearScreen();   // Clear screen before drawing
        drawStepRateGraph();   // Display the step rate graph
        break;
    case 1: // "12H/24H Interval"
        handleTimeFormatSelection();
        drawMenu();
        break;
    case 2:
        // TODO: Set Time
        break;
    case 3:
        // TODO: Set Date
        break;
    case 4: // "Exit"
        inMenu = false;
        forceClockRedraw = true;
        oledC_DrawRectangle(40, 2, 115, 10, OLEDC_COLOR_BLACK);
        oledC_clearScreen();
        break;

    default:
        break;
    }
}

// ---------------- TIMER & USER INITIALIZATION ---------------- //

void Timer_Initialize(void)
{
    TMR1 = 0;
    PR1 = 15625;
    T1CONbits.TCKPS = 3;
    T1CONbits.TCS = 0;
    T1CONbits.TGATE = 0;
    T1CONbits.TON = 1;
}

void Timer1_Interrupt_Initialize(void)
{
    IPC0bits.T1IP = 5;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
}

void User_Initialize(void)
{
    TRISA &= ~(1 << 8 | 1 << 9);
    TRISA |= (1 << 11 | 1 << 12);
    TRISB |= (1 << 12);
    ANSB = 0;
    AD1CON1 = 0;
    AD1CON2 = 0;
    AD1CON3 = 0b001000011111111;
    AD1CHS = 0;
    AD1CHS |= (1 << 3);
    AD1CON1 |= (1 << 15);

    // Set S1 and S2 as inputs
    S1_TRIS = 1;
    S2_TRIS = 1;
}

// ---------------- TIMER1 INTERRUPT (Integrated Menu Handling) ----------------
// Global or file-scope variable to indicate we just entered the menu
static bool justEnteredMenu = false;

void __attribute__((__interrupt__, auto_psv)) _T1Interrupt(void)
{
    incrementTime(&currentTime);
    footToggle = !footToggle;
    globalSeconds++; // Update global seconds counter

    // Long press detection for S1
    static uint8_t s1HoldCounter = 0;
    bool s1State = (PORTAbits.RA11 == 0); // active-low
    if (s1State)
    {
        s1HoldCounter++;
        // For example, 4 ticks = ~2 seconds if each interrupt ~500ms
        if (s1HoldCounter >= 2 && !inMenu)
        {
            inMenu = true;
            selectedMenuItem = 0;
            drawMenu();
            justEnteredMenu = true; // <--- set this flag
            s1HoldCounter = 0;
        }
    }
    else
    {
        s1HoldCounter = 0;
    }

    // If not in menu, do pedometer stuff
    if (!inMenu)
    {
        if (!movementDetected)
            inactivityCounter++;
        else
            inactivityCounter = 0;

        currentSecondIndex = (currentSecondIndex + 1) % HISTORY_SIZE;
        stepsHistory[currentSecondIndex] = 0;
    }

    IFS0bits.T1IF = 0; // Clear interrupt flag
}

// ---------------- MAIN ----------------

int main(void)
{
    int rc;
    uint8_t deviceId = 0;
    SYSTEM_Initialize();
    User_Initialize();
    oledC_setBackground(OLEDC_COLOR_BLACK);
    oledC_clearScreen();
    i2c1_open();

    // Detect the accelerometer
    for (int i = 0; i < 3; i++)
    {
        rc = i2cReadSlaveRegister(WRITE_ADDRESS, 0x00, &deviceId);
        if (rc == OK && deviceId == 0xE5)
            break;
        if (i == 2)
            errorStop("I2C Error or Wrong Device ID");
        DELAY_milliseconds(10);
    }
    initAccelerometer();
    Timer_Initialize();
    Timer1_Interrupt_Initialize();

    // Declare all static variables used for button debouncing and state tracking at the beginning.

    static bool wasInMenu = false;

    while (1)
    {
        if (inMenu)
        {
            // In menu mode: Handle navigation and update the mini clock.
            static bool s1WasPressed = false;
            static bool s2WasPressed = false;

            if (justEnteredMenu)
            {
                // When first entering the menu, wait for the user to release the buttons.
                justEnteredMenu = false;
                bool s1State = (PORTAbits.RA11 == 0);
                bool s2State = (PORTAbits.RA12 == 0);
                s1WasPressed = s1State;
                s2WasPressed = s2State;
            }
            else
            {
                // Poll the button states for short presses.
                bool s1State = (PORTAbits.RA11 == 0);
                bool s2State = (PORTAbits.RA12 == 0);

                // If both buttons are pressed, execute the selected action.
                if (s1State && s2State)
                {
                    executeMenuAction();
                    DELAY_milliseconds(200);
                }
                else {
                    // If S1 is newly pressed, move selection UP.
                    if (s1State && !s1WasPressed)
                    {
                        if (selectedMenuItem > 0)
                            selectedMenuItem--;
                        drawMenu();
                    }
                    // If S2 is newly pressed, move selection DOWN.
                    if (s2State && !s2WasPressed)
                    {
                        if (selectedMenuItem < MENU_ITEMS_COUNT - 1)
                            selectedMenuItem++;
                        drawMenu();
                    }
                }
                // // If S1 is newly pressed, move selection UP.
                // if (s1State && !s1WasPressed)
                // {
                //     if (selectedMenuItem > 0)
                //         selectedMenuItem--;
                //     drawMenu();
                // }
                // // If S2 is newly pressed, move selection DOWN.
                // if (s2State && !s2WasPressed)
                // {
                //     if (selectedMenuItem < MENU_ITEMS_COUNT - 1)
                //         selectedMenuItem++;
                //     drawMenu();
                // }
                // // If both buttons are pressed, execute the selected action.
                // if (s1State && s2State)
                // {
                //     executeMenuAction();
                //     DELAY_milliseconds(200);
                // }
                s1WasPressed = s1State;
                s2WasPressed = s2State;

                // Update the mini clock so it stays current.
                updateMenuClock();
            }
            wasInMenu = true;
        }
        else
        {
            // When not in menu: If we were in menu in the previous loop,
            // clear the mini clock area.
            if (wasInMenu)
            {
                oledC_DrawRectangle(30, 2, 115, 10, OLEDC_COLOR_BLACK);
                wasInMenu = false;
            }

            // Run the normal pedometer/clock functions.
            detectStep();
            drawSteps();
            drawClock(&currentTime);
            oledC_DrawRectangle(0, 0, 15, 15, OLEDC_COLOR_BLACK);
            if (displayedPace > 0)
                drawFootIcon(0, 0, footToggle ? foot1Bitmap : foot2Bitmap, 16, 16);
        }

        DELAY_milliseconds(100);
    }

    return 0;
}
