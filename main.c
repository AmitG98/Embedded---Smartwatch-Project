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

// ---------------- Type and Globals for Set Time ----------------
typedef struct
{
    uint8_t hours;
    uint8_t minutes;
} TimeSetting;

TimeSetting setClock = {8, 24};
// 0 means hours selected; 1 means minutes selected.
uint8_t timeSelection = 0;

// ---------------- Type and Globals for Set Date ----------------
typedef struct
{
    uint8_t day;
    uint8_t month;
} DateSetting;

DateSetting setDate = {24, 1}; // initialize with currentTime.day and currentTime.month
// 0 means day is selected; 1 means month is selected.
uint8_t dateSelection = 0;

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
volatile uint8_t stepsHistory[HISTORY_SIZE] = {0};
static uint8_t currentSecondIndex = 0;
// For smoothing the displayed pace
static float displayedPace = 0.0f;
// Global seconds counter (updated every Timer1 interrupt)
static uint32_t globalSeconds = 0;
bool is12HourFormat = false;
bool inTimeFormatSubpage = false;
bool inTimeSetSubpage = false;
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
#define GRAPH_WIDTH 90
#define GRAPH_HEIGHT 90
#define MAX_STEPS_PER_MINUTE 100  // Maximum steps per minute
uint8_t stepRateHistory[GRAPH_HISTORY_SIZE] = {0};
static uint8_t graphIndex = 0;  // Index to track the current second
bool inGraphMode = false; // Are we currently in the “Graph” sub-page?

// ---------------- Function declaration to avoid implicit warnings ----------------
void updateMenuClock(void);
void drawTimeFormatSubpage(void);
void drawSetTimeMenuBase(void);
void drawSetTimeStatus(void);
void handleSetTimeInput(void);
bool detectTiltForSave(void);
void drawSetDateStatus(void);

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
volatile uint8_t timeDifference = 0;
void updateStepHistory(void) {
    // Ensure that stepsHistory[] has at least 2 entries for calculation
    if (currentSecondIndex > 0) {
        // Time difference between current step and previous step (using floating-point numbers)
        timeDifference = stepsHistory[currentSecondIndex] - stepsHistory[currentSecondIndex - 1];

        // If time difference is greater than 0 (there was a step detected)
        if (timeDifference > 0) {
            // Calculate the instantaneous step rate (steps per minute)
            stepRateHistory[graphIndex] = 60/timeDifference;  // 60 seconds in a minute
        } else {
            // If no time difference (steps detected at the same second), set step rate to 0
            stepRateHistory[graphIndex] = 0;
        }
    }

    // Update the indices to point to the next second in history arrays
    currentSecondIndex = (currentSecondIndex + 1) % HISTORY_SIZE;  // Wrap around after 60 seconds
    graphIndex = (graphIndex + 1) % GRAPH_HISTORY_SIZE;  // Wrap around after 90 seconds
}

void drawMenu(void);

// Draw Step Rate Graph on OLED
void drawStepRateGraph(void) {
    inGraphMode = true;  // Enter graph mode
    oledC_clearScreen();  // Clear screen before drawing

    // int step_rate_history[90] = {
    //     30, 35, 40, 50, 60, 65, 70, 75, 80, 85, 
    //     90, 100, 95, 90, 85, 80, 75, 70, 65, 60, 
    //     55, 50, 55, 60, 65, 70, 80, 85, 90, 85, 
    //     80, 75, 70, 60, 55, 50, 45, 40, 35, 30, 
    //     25, 20, 15, 10, 12, 15, 20, 25, 30, 35, 
    //     40, 45, 50, 55, 60, 70, 75, 80, 85, 90, 
    //     95, 90, 80, 75, 70, 65, 60, 55, 50, 45, 
    //     40, 35, 30, 25, 20, 15, 10, 15, 20, 25, 
    //     30, 35, 40, 45, 50, 55, 60, 70, 75, 80, 
    //     85, 90, 95, 100, 95, 90, 85, 80, 75, 70
    // };

    // Drawing the X axis (time axis)
    oledC_DrawLine(20, GRAPH_HEIGHT, GRAPH_WIDTH, GRAPH_HEIGHT, 1, 0xFFFF); // Horizontal line for X-axis

    // Draw horizontal grid lines at 30, 60, and 100 steps
    int step_values[] = {0, 30, 60, 100};
    int min_y = GRAPH_HEIGHT; 
    int max_y = 10; 

    // Add labels to Y axis (step rate)
    for (int i = 1; i < 4; i++) {
        // int y_pos = GRAPH_HEIGHT - 10 - (step_values[i] * (GRAPH_HEIGHT - 20) / 100);
        int y_pos = min_y - ((step_values[i] * (min_y - max_y)) / 100);
        char label[4];
        sprintf(label, "%d", step_values[i]);
        // oledC_DrawString(0, y_pos - 4, 1, 1, label, 0xFFFF); 
        oledC_DrawString(0, y_pos - 4, 1, 1, (uint8_t *)label, 0xFFFF); // Cast char* to uint8_t*

        // Draw horizontal dotted grid line at this Y-position
        for (int j = 0; j < 30; j++) { 
            int x_pos = 20 + (j * (GRAPH_WIDTH - 20) / 29); 
            oledC_DrawPoint(x_pos, y_pos, 0xFFFF);
        }
    }

    // Draw X-axis as small dots for 90 seconds
    for (int i = 0; i <= 9; i++) {
        int x_pos = 20 + (i * (GRAPH_WIDTH - 20) / 9);
        oledC_DrawThickPoint(x_pos, GRAPH_HEIGHT - 2, 1.5, 0xFFFF);
    }

    // // Plot the fake step rate history
    // for (int i = 0; i < 90; i++) {
    //     // Calculate the X position based on the second (i represents the second)
    //     int x_pos = 20 + (i * (GRAPH_WIDTH - 20) / 89); 

    //     // Map the step rate value (0 to 100) to the Y-axis (invert the Y calculation)
    //     int y_pos = min_y - (step_rate_history[i] * (min_y - max_y) / 100);

    //     // Plot the step rate history as a point
    //     oledC_DrawPoint(x_pos, y_pos, 0xFFFF);
    // }

    for (int i = 0; i < 90; i++) {
        // Calculate the X position based on the second (i represents the second)
        int x_pos = 20 + (i * (GRAPH_WIDTH - 20) / 89); 

        // Map the step rate value (0 to 100) to the Y-axis (invert the Y calculation)
        int y_pos = min_y - (stepRateHistory[i] * (min_y - max_y) / 100);

        // Plot the step rate history as a point
        oledC_DrawPoint(x_pos, y_pos, 0xFFFF);
    }

    // Plot the real step rate history (from the accelerometer)
    // for (int i = 0; i < GRAPH_HISTORY_SIZE; i++) {
    //     int y_pos = GRAPH_HEIGHT - (stepRateHistory[i] * GRAPH_HEIGHT / 100);

    //     // Map the time (0 to 89 seconds) to the X-axis
    //     int x_pos = 20 + (i * (GRAPH_WIDTH - 20) / (GRAPH_HISTORY_SIZE - 1));  // Spread over the 90 seconds

    //     // Plot the data point on the graph (white point for steps)
    //     oledC_DrawPoint(x_pos, y_pos, 0xFFFF);  // Plot a white point at (x_pos, y_pos)

    //     // int x_pos = 20 + (i * (GRAPH_WIDTH - 20) / (GRAPH_HISTORY_SIZE - 1));  // 90 seconds, 89 intervals
    //     // int y_pos = min_y - (stepRateHistory[i] * (min_y - max_y) / 100);  // Map the step rate value (0 to 100) to the Y-axis

    //     // oledC_DrawPoint(x_pos, y_pos, 0xFFFF);  // Plot the step rate history as a point
    // }

    bool s1WasPressed = false;
    bool s2WasPressed = false;

    while (inGraphMode)
    {
        bool s1State = (PORTAbits.RA11 == 0);  
        bool s2State = (PORTAbits.RA12 == 0);  

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
    // oledC_DrawString(0, 20, 1, 1, (uint8_t *)msg, OLEDC_COLOR_DARKRED);
    // printf("Error: %s\n", msg);
    // for (;;)
    //     ;
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

// ---------------- SET TIME SYSTEM ---------------- //
void drawSetTimeMenuBase(void)
{
    oledC_clearScreen();

    // Clear any mini clock area.
    oledC_DrawRectangle(30, 2, 115, 10, OLEDC_COLOR_BLACK);

    // Display header.
    oledC_DrawString(6, 10, 2, 2, (uint8_t *)"Set Time", OLEDC_COLOR_WHITE);

    if (timeSelection == 0)
    {
        // Hours selected: draw hours box with white border and black fill.
        oledC_DrawRectangle(8, 40, 44, 64, OLEDC_COLOR_WHITE);
        oledC_DrawRectangle(10, 42, 42, 62, OLEDC_COLOR_BLACK);

        // Draw minute box completely black (shifted left).
        oledC_DrawRectangle(50, 40, 86, 64, OLEDC_COLOR_BLACK);
        oledC_DrawRectangle(52, 42, 84, 62, OLEDC_COLOR_BLACK);
    }
    else
    {
        // Minutes selected: draw minute box with white border and black fill.
        oledC_DrawRectangle(8, 40, 44, 64, OLEDC_COLOR_BLACK);
        oledC_DrawRectangle(10, 42, 42, 62, OLEDC_COLOR_BLACK);

        oledC_DrawRectangle(50, 40, 86, 64, OLEDC_COLOR_WHITE);
        oledC_DrawRectangle(52, 42, 84, 62, OLEDC_COLOR_BLACK);
    }

    drawSetTimeStatus();
}

// Update the displayed numbers (hours and minutes).
void drawSetTimeStatus(void)
{
    char buf[3];

    // Clear the region where the hour value is drawn.
    oledC_DrawRectangle(15, 46, 43, 62, OLEDC_COLOR_BLACK);
    sprintf(buf, "%02d", setClock.hours);
    oledC_DrawString(15, 46, 2, 2, (uint8_t *)buf, OLEDC_COLOR_WHITE);

    // Clear the region where the minute value is drawn.
    oledC_DrawRectangle(55, 46, 83, 62, OLEDC_COLOR_BLACK);
    sprintf(buf, "%02d", setClock.minutes);
    oledC_DrawString(55, 46, 2, 2, (uint8_t *)buf, OLEDC_COLOR_WHITE);
}

// Handle input for the Set Time page using RA11 (S1) and RA12 (S2).
void handleSetTimeInput(void)
{
    // Using the same structure as in your 12H/24H page.
    static bool s1WasPressed = false;
    static bool s2WasPressed = false;

    // Read buttons using RA11 and RA12.
    bool s1State = (PORTAbits.RA11 == 0);
    bool s2State = (PORTAbits.RA12 == 0);

    // If both buttons are pressed (new press), toggle the selected field.
    if (s1State && s2State && !s1WasPressed && !s2WasPressed)
    {
        timeSelection = !timeSelection;
        drawSetTimeMenuBase();
    }
    // If S1 is pressed alone, increase the selected value.
    else if (s1State && !s1WasPressed && !(s1State && s2State))
    {
        if (timeSelection == 0)
            setClock.hours = (setClock.hours + 1) % 24;
        else
            setClock.minutes = (setClock.minutes + 1) % 60;
        drawSetTimeStatus();
    }
    // If S2 is pressed alone, decrease the selected value.
    else if (s2State && !s2WasPressed && !(s1State && s2State))
    {
        if (timeSelection == 0)
            setClock.hours = (setClock.hours == 0) ? 23 : setClock.hours - 1;
        else
            setClock.minutes = (setClock.minutes == 0) ? 59 : setClock.minutes - 1;
        drawSetTimeStatus();
    }

    s1WasPressed = s1State;
    s2WasPressed = s2State;
}
bool detectTiltForSave(void)
{
    ACCEL_DATA_t accel;
    accel.x = readAxis(REG_DATAX0);
    accel.y = readAxis(REG_DATAY0);
    accel.z = readAxis(REG_DATAZ0);

    // Adjust the threshold as needed for your device sensitivity.
    const float tiltThreshold = 700.0f;

    // Convert raw values to a magnitude.
    float ax = accel.x * 4.0f;
    float ay = accel.y * 4.0f;
    float az = accel.z * 4.0f;
    float magnitude = sqrtf(ax * ax + ay * ay + az * az);

    // If the magnitude is below the threshold, we consider that a tilt save gesture.
    return (magnitude < tiltThreshold);
}

void handleSetTimePage(void)
{
    inTimeSetSubpage = true; // Enter Set Time page.

    // Initialize temporary time values from currentTime.
    setClock.hours = currentTime.hours;
    setClock.minutes = currentTime.minutes;
    timeSelection = 0; // Start with hours selected.

    drawSetTimeMenuBase();

    // Wait until both buttons are released.
    while ((PORTAbits.RA11 == 0) || (PORTAbits.RA12 == 0))
    {
        DELAY_milliseconds(10);
    }

    int tiltCounter = 0; // Debounce counter for the tilt
    // Poll for tilt while handling input.
    while (inTimeSetSubpage)
    {
        handleSetTimeInput();

        if (detectTiltForSave())
        {
            tiltCounter++;
            // Save if tilt is detected for one iteration (~50ms)
            if (tiltCounter >= 1)
            {
                currentTime.hours = setClock.hours;
                currentTime.minutes = setClock.minutes;
                currentTime.seconds = 0; // Reset seconds to 00

                inTimeSetSubpage = false;
                break;
            }
        }
        else
        {
            tiltCounter = 0; // Reset if no tilt is detected.
        }

        DELAY_milliseconds(50); // Reduced delay for quicker response
    }
}

// ---------------- SET DATE SYSTEM ---------------- //
void drawSetDateMenuBase(void)
{
    oledC_clearScreen();

    // Clear any mini clock area.
    oledC_DrawRectangle(30, 2, 115, 10, OLEDC_COLOR_BLACK);

    // Display header.
    oledC_DrawString(6, 10, 2, 2, (uint8_t *)"Set Date", OLEDC_COLOR_WHITE);

    if (dateSelection == 0)
    {
        // Day selected: draw day box with white border and black fill.
        oledC_DrawRectangle(8, 40, 44, 64, OLEDC_COLOR_WHITE);
        oledC_DrawRectangle(10, 42, 42, 62, OLEDC_COLOR_BLACK);

        // Draw month box completely black.
        oledC_DrawRectangle(50, 40, 86, 64, OLEDC_COLOR_BLACK);
        oledC_DrawRectangle(52, 42, 84, 62, OLEDC_COLOR_BLACK);
    }
    else
    {
        // Month selected: draw month box with white border and black fill.
        oledC_DrawRectangle(8, 40, 44, 64, OLEDC_COLOR_BLACK);
        oledC_DrawRectangle(10, 42, 42, 62, OLEDC_COLOR_BLACK);

        oledC_DrawRectangle(50, 40, 86, 64, OLEDC_COLOR_WHITE);
        oledC_DrawRectangle(52, 42, 84, 62, OLEDC_COLOR_BLACK);
    }

    drawSetDateStatus();
}

void drawSetDateStatus(void)
{
    char buf[3];

    // Clear and draw the day value.
    oledC_DrawRectangle(15, 46, 43, 62, OLEDC_COLOR_BLACK);
    sprintf(buf, "%02d", setDate.day);
    oledC_DrawString(15, 46, 2, 2, (uint8_t *)buf, OLEDC_COLOR_WHITE);

    // Clear and draw the month value.
    oledC_DrawRectangle(55, 46, 83, 62, OLEDC_COLOR_BLACK);
    sprintf(buf, "%02d", setDate.month);
    oledC_DrawString(55, 46, 2, 2, (uint8_t *)buf, OLEDC_COLOR_WHITE);
}

void handleSetDateInput(void)
{
    static bool s1WasPressed = false;
    static bool s2WasPressed = false;

    bool s1State = (PORTAbits.RA11 == 0);
    bool s2State = (PORTAbits.RA12 == 0);

    // If both buttons are pressed (new press), toggle the selected field.
    if (s1State && s2State && !s1WasPressed && !s2WasPressed)
    {
        dateSelection = !dateSelection;
        drawSetDateMenuBase();
    }
    // If S1 is pressed alone, increase the selected value.
    else if (s1State && !s1WasPressed && !(s1State && s2State))
    {
        if (dateSelection == 0) // adjust day
        {
            uint8_t maxDay = daysInMonth[setDate.month - 1];
            setDate.day = (setDate.day % maxDay) + 1;
        }
        else // adjust month
        {
            setDate.month = (setDate.month % 12) + 1;
            // Ensure day does not exceed the new month's max.
            uint8_t maxDay = daysInMonth[setDate.month - 1];
            if (setDate.day > maxDay)
                setDate.day = maxDay;
        }
        drawSetDateStatus();
    }
    // If S2 is pressed alone, decrease the selected value.
    else if (s2State && !s2WasPressed && !(s1State && s2State))
    {
        if (dateSelection == 0) // adjust day
        {
            if (setDate.day == 1)
                setDate.day = daysInMonth[setDate.month - 1];
            else
                setDate.day--;
        }
        else // adjust month
        {
            if (setDate.month == 1)
                setDate.month = 12;
            else
                setDate.month--;
            uint8_t maxDay = daysInMonth[setDate.month - 1];
            if (setDate.day > maxDay)
                setDate.day = maxDay;
        }
        drawSetDateStatus();
    }

    s1WasPressed = s1State;
    s2WasPressed = s2State;
}

void handleSetDatePage(void)
{
    inTimeSetSubpage = true; // Reuse the same flag for a subpage.

    // Initialize temporary date values from currentTime.
    setDate.day = currentTime.day;
    setDate.month = currentTime.month;
    dateSelection = 0; // Start with day selected.

    drawSetDateMenuBase();

    // Wait until both buttons are released.
    while ((PORTAbits.RA11 == 0) || (PORTAbits.RA12 == 0))
    {
        DELAY_milliseconds(10);
    }

    int tiltCounter = 0; // Debounce counter.
    while (inTimeSetSubpage)
    {
        handleSetDateInput();

        if (detectTiltForSave())
        {
            tiltCounter++;
            // Save if a tilt is detected (using your chosen sensitivity).
            if (tiltCounter >= 1)
            {
                currentTime.day = setDate.day;
                currentTime.month = setDate.month;
                // Exit the set date page.
                inTimeSetSubpage = false;
                break;
            }
        }
        else
        {
            tiltCounter = 0;
        }

        DELAY_milliseconds(50);
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

// bool inMenu = false;
uint8_t selectedMenuItem = 0;

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

void executeMenuAction(void)
{
    switch (selectedMenuItem)
    {
    case 0:
        // oledC_clearScreen();   // Clear screen before drawing
        drawStepRateGraph();   // Display the step rate graph
        break;
    case 1: // "12H/24H Interval"
        handleTimeFormatSelection();
        drawMenu();
        break;
    case 2: // "Set Time"
        // inMenu = false;
        handleSetTimePage();
        drawMenu();
        break;
    case 3: // "Set Date"
        handleSetDatePage();
        drawMenu();
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

        detectStep();
        updateStepHistory();
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
                // Poll the button states.
                bool s1State = (PORTAbits.RA11 == 0);
                bool s2State = (PORTAbits.RA12 == 0);

                // If both buttons are pressed, execute the selected action and delay.
                if (s1State && s2State)
                {
                    executeMenuAction();
                    DELAY_milliseconds(200); // Wait 200ms to allow for debouncing.
                }
                else
                {
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
                s1WasPressed = s1State;
                s2WasPressed = s2State;

                // Update the mini clock so it stays current.
                updateMenuClock();
            }
            wasInMenu = true;
        }
        else
        {
            // When not in menu: If we were in menu in the previous loop, clear the mini clock area.
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
