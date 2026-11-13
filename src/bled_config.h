/**
 * @file bled_config.h
 * @brief Build-time configuration for the BLED display unit (Blood Loss
 *        Estimation Device) - everything you may want to change lives here.
 *
 * CrowPanel Advance 7.0" (DIS02170A, hardware v1.4):
 *
 *   - TF (SD) slot, SPI mode:  MOSI = IO6, MISO = IO4, SCK = IO5.
 *     The card's CS line is NOT wired to any GPIO (Elecrow's own examples
 *     pass a dummy CS pin), so BLED_SD_PIN_CS_DUMMY is only a placeholder
 *     that the SPI driver toggles - it must be a GPIO that nothing else
 *     uses.  IO8 is unused by this project (it is only routed to the
 *     optional wireless-module socket).
 *   - UART0-OUT port:  RX = IO44, TX = IO43  (names from the display's
 *     point of view).  This is also the console / USB-serial UART, so in
 *     real-link mode the display's own log output is switched off and the
 *     USB-serial cable must NOT be plugged in while the unit is connected.
 *   - The back DIP switches S1/S0 select what IO4/IO5/IO6 (SD card / speaker
 *     I2S) and IO19/IO20 (UART1-OUT / I2S microphone) are connected to.
 *     Board v1.4 (silkscreen: 0/0 MIC+SPK, 0/1 wireless module, 1/1 MIC+TF
 *     card, no 1/0).  Use 1/1 : it connects the SD slot.  The microphone then
 *     owns IO19/IO20, which is why the unit link is NOT on UART1-OUT
 *     (tested: UART1 works at 0/1 but dies at 1/1).  IO43/IO44 are not
 *     shared with the DIP switches.
 *     The on-board speaker amp cannot play sound with the SD card enabled; the
 *     buzzer (driven through the STC8H1K28) is unaffected.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define BLED_FW_VERSION         "0.1.0"
#define BLED_DEVICE_NAME        "BLED"
#define BLED_DEVICE_SUBTITLE    "Blood Loss Estimation"

/* ------------------------------------------------------------------ */
/* Measurement-unit link                                              */
/* ------------------------------------------------------------------ */

/** 1 = use the built-in SIMULATED measurement unit (bench demo: no wires
 *      needed; a clearly visible "SIM" badge is shown in the status bar).
 *  0 = talk to the real measurement unit over UART0-OUT.  Set this to 0 once
 *      the measurement board is connected. */
#ifndef BLED_LINK_SIMULATE
#define BLED_LINK_SIMULATE      0
#endif

#define BLED_LINK_UART_NUM      0        /* UART_NUM_0 = UART0-OUT port */
#define BLED_LINK_PIN_TX        43       /* display TX  -> measurement unit RX */
#define BLED_LINK_PIN_RX        44       /* display RX  <- measurement unit TX */
#define BLED_LINK_BAUD          115200
#define BLED_LINK_PING_MS       1000     /* heartbeat sent to the unit */
#define BLED_LINK_TIMEOUT_MS    3000     /* unit is "offline" after this long without a valid frame */

/* ------------------------------------------------------------------ */
/* SD card (TF slot, SPI mode)                                        */
/* ------------------------------------------------------------------ */

#define BLED_SD_MOUNT_POINT     "/sdcard"
#define BLED_SD_DIR_NAME        "BLED"       /* records live in /sdcard/BLED (8.3-safe: FATFS long names are disabled) */
#define BLED_SD_PIN_MOSI        6
#define BLED_SD_PIN_MISO        4
#define BLED_SD_PIN_SCK         5
#define BLED_SD_PIN_CS_DUMMY    8
#define BLED_SD_FREQ_KHZ        20000
#define BLED_SD_RETRY_MS        4000         /* re-try mounting this often while no card is ready */
#ifndef BLED_SD_ENABLE
#define BLED_SD_ENABLE          1            /* 0 = never touch the SD slot (diagnostics: rules the SD code in/out) */
#endif
#define BLED_SD_START_DELAY_MS  2500         /* first mount attempt is made this long after boot, once the UI is up */

/* ------------------------------------------------------------------ */
/* Case data limits                                                   */
/* ------------------------------------------------------------------ */

#define BLED_CASE_ID_MAX        12           /* characters, not counting the terminator */
#define BLED_CASE_ID_CHARS      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"
#define BLED_MAX_PADS           200          /* gauze pads recorded per case */
#define BLED_MAX_RECORDS        200          /* cases listed in the Records tab (newest first) */
#define BLED_PAD_EBL_MAX_ML     500.0f       /* a single pad above this is rejected as a sensor error */

/** Patient baseline hemoglobin entry limits (g/dL). */
#define BLED_HB_MIN_GDL         4.0f
#define BLED_HB_MAX_GDL         20.0f

/** Cumulative blood-loss colour thresholds (mL).  PLACEHOLDER VALUES: the
 *  clinical team / project sponsor must decide the real limits (they are
 *  normally patient-specific, e.g. a percentage of estimated blood volume).
 *  They only change the colour of the on-screen number - nothing else. */
#define BLED_EBL_WARN_ML        500.0f       /* amber at or above */
#define BLED_EBL_ALERT_ML       1000.0f      /* red at or above   */

#ifdef __cplusplus
}
#endif
