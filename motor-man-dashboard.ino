// ESP32 program for a CYD that sits on the dash of Little Blue https://LittleBleu.info a
// 1968 Taylor-Dunn (or other cart) and receives updates from the ESP32 via ESP-NOW from 
// the controller connected to the Motor via the Curtis Controller. This will display speeds and allow
// for various modes of driving. (Kid mode, offroad, Sreet) all wirelessly.
//
// IMPORTANT NOTES:
// 1. Parameter Codes: You MUST replace the "XXXX", "YYYY", "ZZZZ" placeholders
//    with the actual 4-character hex parameter codes for your specific Curtis 1205M model
//    Sources: Reverse-engineering with an official programmer, or specialized forums.
// 2. ESP-NOW Peer MAC Address: Ensure 'peerMacAddress' is set to the MAC address
//    of the ESP-NOW device you want to communicate with. For broadcast, use all 0xFF.
// 3. Data Interpretation: The raw hex values read from Curtis need to be correctly
//    interpreted (e.g., converted to Celsius for temperature, actual volts for voltage).
//    This depends on the controller's internal scaling and sensor types.

#include <HardwareSerial.h>
#include <esp_now.h>
#include <WiFi.h>
#include <TFT_eSPI.h> // Graphics and font library for ESP32 and ESP8266 boards
#include <XPT2046_Touchscreen.h> // Touchscreen driver for XPT2046

// --- CYD Display and Touch Configuration ---
// Adjust these pins according to your specific CYD board
#define TFT_CS   5
#define TFT_DC   27
#define TFT_RST  -1  // Set to -1 if display reset is not connected
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_MISO 19

#define TS_CS    22
#define TS_IRQ   36  // Often GPIO 36 (VP) or 39 (VN) for touch IRQ on ESP32

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
XPT2046_Touchscreen ts(TS_CS, TS_IRQ); // Param 2 is interrupt pin

// Define screen states
enum ScreenState {
  MAIN_MENU,
  DASH_SCREEN,
  CONSOLE_SCREEN,
  LOCK_SCREEN
};

ScreenState currentScreen = MAIN_MENU;

// --- Curtis Serial Configuration ---
HardwareSerial CurtisSerial(2); // Using UART2 (Serial2) for Curtis communication

// Define the GPIO pins for Serial2. Adjust these as per your board.
const int RXD2 = 16; // RX pin for ESP32 UART2 (connects to TX of your Curtis interface)
const int TXD2 = 17; // TX pin for ESP32 UART2 (connects to RX of your Curtis interface)

const long BAUD_RATE = 9600;
const byte DATA_BITS = SERIAL_8N1; // 8 data bits, no parity, 1 stop bit
const unsigned long SERIAL_TIMEOUT = 500; // milliseconds to wait for a response

// Common polling character that some Curtis controllers expect periodically
const char POLLING_CHAR = '$';
// Common initial connection sequence.
const char* INITIAL_CONNECT_SEQ = ":PG2D";

// Keep track of Curtis connection status
bool isCurtisConnected = false;

// Structure to hold Curtis parameter details
struct CurtisParameter {
  const char* code;    // 4-char hex code for the parameter
  const char* name;    // Human-readable name
  String lastValue;    // Last polled value from the controller (raw hex/decimal)
  // int type; // For future enhancements: could define type for proper scaling/conversion
};

// Define the list of parameters to poll.
// !!! REPLACE "XXXX", "YYYY", "ZZZZ" with actual parameter codes for your Curtis 1205M !!!
CurtisParameter curtisParameters[] = {
  {"6F4C", "Motor Temp", ""},    // Example: Motor housing temperature (Example code, verify for your model)
  {"1001", "Battery Volts", ""}, // Example: Battery voltage (Example code, verify for your model)
  {"6F1A", "Throttle Pos", ""}   // Example: Throttle position (Example code, verify for your model)
};
const int NUM_CURTIS_PARAMS = sizeof(curtisParameters) / sizeof(curtisParameters[0]);

// --- ESP-NOW Configuration ---
// MAC address of the peer ESP-NOW device.
// Use {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} for broadcast to all devices.
// For specific communication, replace with the actual MAC address of the receiver.
uint8_t peerMacAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Structure for data to be sent via ESP-NOW (polled values)
// Max payload size for ESP-NOW is 250 bytes. Adjust string lengths as needed.
typedef struct struct_message_tx {
  char motorTemp[16];   // Null-terminated string for motor temp value
  char battVolts[16];   // Null-terminated string for battery voltage value
  char throttlePos[16]; // Null-terminated string for throttle position value
  // Add more fields if polling more parameters, ensuring total size < 250 bytes
} struct_message_tx;

// Structure for data to be received via ESP-NOW (set command)
typedef struct struct_message_rx {
  char paramCode[8];  // Parameter code to set (e.g., "001A")
  char newValue[16];  // New value for the parameter (e.g., "C0")
} struct_message_rx;

struct_message_tx myDataTx; // Data to send
struct_message_rx myDataRx; // Data received

// Callback when data is sent via ESP-NOW
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("ESP-NOW: Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// Callback when data is received via ESP-NOW
void OnDataRecv(const esp_now_recv_info *recv_info , const uint8_t *incomingData, int len) {

  // Access the MAC address from recv_info
  const uint8_t *mac_addr = recv_info->src_addr;
  Serial.print("\nESP-NOW: Received 'set' command from MAC: ");
  for (int i = 0; i < 6; ++i) {
    Serial.printf("%02X:", mac_addr[i]);
    Serial.printf("%02X%s", mac_addr[i], (i < 5 ? ":" : ""));
  }
  Serial.println();

  // Make sure the data size matches your struct
  if (len == sizeof(struct_message_rx)) {
    memcpy(&myDataRx, incomingData, sizeof(myDataRx));
    Serial.println("Received Data:");
   
    Serial.println();
    Serial.print("  Param Code: ");
    Serial.println(myDataRx.paramCode);
    Serial.print("  New Value: ");
    Serial.println(myDataRx.newValue);

    // Attempt to write the received parameter to Curtis
    // Only attempt write if Curtis is connected
    if (isCurtisConnected) {
      // Look up the parameter in our defined list (optional, but good for validation)
      bool paramFound = false;
      for (int i = 0; i < NUM_CURTIS_PARAMS; i++) {
        if (strcmp(curtisParameters[i].code, myDataRx.paramCode) == 0) {
          paramFound = true;
          break;
        }
      }

      if (paramFound) {
        if (writeCurtisParameter(myDataRx.paramCode, myDataRx.newValue)) {
          Serial.println("Curtis parameter updated via ESP-NOW.");
          // Optionally, immediately poll the updated value to confirm and broadcast
          // For simplicity, it will be updated in the next regular polling cycle.
        } else {
          Serial.println("Failed to update Curtis parameter via ESP-NOW (Curtis write failed).");
        }
      } else {
        Serial.print("Received param code '");
        Serial.print(myDataRx.paramCode);
        Serial.println("' not recognized in defined list. Ignoring write.");
      }
    } else {
      Serial.println("Curtis controller not connected. Cannot perform write operation.");
    }

  } else {
    Serial.print("Received data of unexpected length: ");
    Serial.println(len);
    // You might want to print raw data or handle this differently
  }

}

// --- Curtis Protocol Helper Functions ---

// Calculates the XOR checksum for a given command string
byte calculateXORChecksum(const String& cmd) {
  byte checksum = 0;
  // Common for older Curtis serial: XOR sum of all characters BEFORE the checksum itself.
  for (int i = 0; i < cmd.length(); i++) {
    checksum ^= cmd.charAt(i);
  }
  return checksum;
}

// Sends a read command to Curtis and returns the raw parsed value string
String readCurtisParameter(const char* paramCode) {
  String command = ":R";
  command += paramCode;
  Serial.print("Sending read command: ");
  Serial.println(command);
  CurtisSerial.print(command);

  // Read the response from the Curtis controller
  String rawResponse = CurtisSerial.readStringUntil('\n'); // Adjust end delimiter if needed
  rawResponse.trim();
  Serial.print("Raw Curtis response: '");
  Serial.print(rawResponse);
  Serial.println("'");

  // Basic parsing for the value. Needs to be more robust for real applications.
  // Assuming response format: "XXXXVALUE;" or "VALUE;" or just "VALUE".
  String parsedValue = "";
  if (rawResponse.startsWith("#")) { // Error character
    Serial.println("Curtis controller responded with an error (#).");
    return "";
  }

  int valueStartIndex = rawResponse.indexOf(paramCode);
  if (valueStartIndex != -1) {
    // If the parameter code is echoed back, try to extract value after it.
    parsedValue = rawResponse.substring(valueStartIndex + strlen(paramCode)); // Skip param code
  } else {
    // If the parameter code is not echoed, assume the response is just the value
    // This is very simplified, depends heavily on actual response
    parsedValue = rawResponse;
  }

  // Remove any trailing ';' or other non-hex characters for parsing
  int semicolonIndex = parsedValue.indexOf(';');
  if (semicolonIndex != -1) {
    parsedValue = parsedValue.substring(0, semicolonIndex);
  }
  parsedValue.trim();

  if (parsedValue.length() == 0) {
    Serial.println("Error parsing Curtis response: empty value extracted.");
    return "";
  }
  return parsedValue;
}

// Sends a write command to Curtis
bool writeCurtisParameter(const char* paramCode, const char* newValue) {
  String commandBase = ":W";
  commandBase += paramCode;
  commandBase += newValue;
  commandBase += 'G'; // Common delimiter before checksum

  byte checksum = calculateXORChecksum(commandBase);
  char checksumHex[3]; // 2 characters for hex, 1 for null terminator
  sprintf(checksumHex, "%02X", checksum); // Convert byte checksum to two-char hex string

  String fullCommand = commandBase + checksumHex;
  Serial.print("Sending write command: ");
  Serial.println(fullCommand);
  CurtisSerial.print(fullCommand);

  String response = CurtisSerial.readStringUntil(';'); // Expect ';' acknowledgment
  response.trim();

  if (response.endsWith(";")) {
    // The controller might also echo back the command before the ;
    if (response.indexOf(fullCommand.substring(0, fullCommand.length() - 2)) != -1 || response.indexOf(";") != -1) {
      Serial.println("Curtis write successful (received ';').");
      return true;
    }
  } else {
    Serial.print("Curtis write failed or no ACK: '");
    Serial.print(response);
    Serial.println("'");
  }
  return false;
}

// --- GUI Functions ---

void drawMainMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 20);
  tft.println("Curtis Controller");
  tft.setCursor(10, 50);
  tft.println("Menu");

  // Button: Dash
  tft.drawRect(50, 100, 140, 50, TFT_CYAN);
  tft.setTextDatum(MC_DATUM); // Middle-Centre datum
  tft.drawString("Dash", 120, 125);

  // Button: Console
  tft.drawRect(50, 170, 140, 50, TFT_GREEN);
  tft.drawString("Console", 120, 195);

  // Button: Lock
  tft.drawRect(50, 240, 140, 50, TFT_RED);
  tft.drawString("Lock", 120, 265);
}

void drawDashScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("Dash - Live Data");

  // Display polled values
  tft.setTextFont(2);
  int yPos = 50;
  for (int i = 0; i < NUM_CURTIS_PARAMS; i++) {
    tft.setCursor(10, yPos);
    tft.printf("%s: %s", curtisParameters[i].name, curtisParameters[i].lastValue.c_str());
    yPos += 30;
  }

  // Back to Menu button
  tft.drawRect(TFT_WIDTH - 120, TFT_HEIGHT - 40, 110, 30, TFT_BLUE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Back", TFT_WIDTH - 65, TFT_HEIGHT - 25);
}

void drawConsoleScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("Console - Serial Out");

  // For now, this will display the last few lines of serial output.
  // A more robust solution would involve buffering serial data.
  // For this example, we'll just show a placeholder.
  tft.setTextFont(2);
  tft.setCursor(10, 50);
  tft.println("Showing raw serial data...");
  tft.setCursor(10, 80);
  tft.println("Implement buffering here!");

  // Back to Menu button
  tft.drawRect(TFT_WIDTH - 120, TFT_HEIGHT - 40, 110, 30, TFT_BLUE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Back", TFT_WIDTH - 65, TFT_HEIGHT - 25);
}

void drawLockScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(4);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("Lock Status");
  tft.setTextFont(6);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 100);
  tft.println("LOCKED!"); // This would be dynamic based on a Curtis parameter

  // Back to Menu button
  tft.drawRect(TFT_WIDTH - 120, TFT_HEIGHT - 40, 110, 30, TFT_BLUE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Back", TFT_WIDTH - 65, TFT_HEIGHT - 25);
}


void handleTouch() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();

    // Map the touch coordinates to display coordinates (calibration might be needed)
    // Basic mapping for 240x320 display assuming landscape
    // p.x is 0-4096, p.y is 0-4096
    int x = map(p.y, 0, 4096, 0, tft.width()); // XPT2046 often has Y as display X
    int y = map(p.x, 0, 4096, 0, tft.height()); // XPT2046 often has X as display Y

    Serial.printf("Touch detected at: (%d, %d)\n", x, y); // Debug touch

    if (currentScreen == MAIN_MENU) {
      if (x > 50 && x < 190 && y > 100 && y < 150) { // Dash button
        Serial.println("Dash button pressed");
        currentScreen = DASH_SCREEN;
        drawDashScreen();
      } else if (x > 50 && x < 190 && y > 170 && y < 220) { // Console button
        Serial.println("Console button pressed");
        currentScreen = CONSOLE_SCREEN;
        drawConsoleScreen();
      } else if (x > 50 && x < 190 && y > 240 && y < 290) { // Lock button
        Serial.println("Lock button pressed");
        currentScreen = LOCK_SCREEN;
        drawLockScreen();
      }
    } else { // On Dash, Console, or Lock screen
      if (x > (TFT_WIDTH - 120) && x < TFT_WIDTH - 10 && y > (TFT_HEIGHT - 40) && y < TFT_HEIGHT - 10) { // Back button
        Serial.println("Back button pressed");
        currentScreen = MAIN_MENU;
        drawMainMenu();
      }
    }
    delay(300); // Debounce
  }
}

// --- Setup Function ---
void setup() {
  // Initialize USB Serial for debugging output
  Serial.begin(460800);
  while (!Serial); // Wait for Serial Monitor to be open
  Serial.println("--- ESP32 Curtis 1205M ESP-NOW Poller/Setter ---");

  // Initialize Display
  tft.init();
  tft.setRotation(3); // Adjust rotation as needed (0, 1, 2, 3) for landscape/portrait
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  tft.println("Display Initialized.");

  // Initialize Touchscreen
  ts.begin();
  ts.setRotation(3); // Match display rotation
  tft.println("Touchscreen Initialized.");

  drawMainMenu(); // Draw the initial menu screen

  // Initialize Curtis Serial interface
  CurtisSerial.begin(BAUD_RATE, DATA_BITS, RXD2, TXD2);
  CurtisSerial.setTimeout(SERIAL_TIMEOUT); // Set a timeout for reading responses

  Serial.print("Curtis Serial (UART2) initialized on RXD: ");
  Serial.print(RXD2);
  Serial.print(", TXD: ");
  Serial.println(TXD2);
  Serial.println("Remember to use a proper inverted TTL RS-232 converter!");
  tft.println("Curtis Serial Init.");

  // Display placeholder warnings
  for (int i = 0; i < NUM_CURTIS_PARAMS; i++) {
    if (strcmp(curtisParameters[i].code, "XXXX") == 0 ||
        strcmp(curtisParameters[i].code, "YYYY") == 0 ||
        strcmp(curtisParameters[i].code, "ZZZZ") == 0) {
      Serial.print("WARNING: Parameter '");
      Serial.print(curtisParameters[i].name);
      Serial.print("' has placeholder code '");
      Serial.print(curtisParameters[i].code);
      Serial.println("'. Please find the correct parameter code!");
    }
  }

  // Initialize ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    tft.println("ESP-NOW Error!");
    return;
  }
  Serial.println("ESP-NOW initialized.");
  tft.println("ESP-NOW Initialized.");

  // Register ESP-NOW callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Add peer for ESP-NOW communication
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, peerMacAddress, 6);
  peerInfo.channel = 0; // Use default channel
  peerInfo.encrypt = false; // No encryption
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer.");
    tft.println("ESP-NOW Peer Fail.");
    return;
  }

  Serial.println("ESP-NOW peer added. Ready to send/receive.");
  Serial.print("This device's MAC: ");
  Serial.println(WiFi.macAddress());
  tft.println("ESP-NOW Peer Added.");
  delay(100000); // Give time for messages to display
  drawMainMenu(); // Redraw main menu after initial setup messages
}

// --- Main Loop Function ---
void loop() {
  handleTouch(); // Check for touch input in every loop

  // 1. Establish/Maintain Curtis Connection (runs in background)
  if (!isCurtisConnected) {
    Serial.println("\nAttempting to connect to Curtis controller...");
    CurtisSerial.print(INITIAL_CONNECT_SEQ);
    delay(100); // Small delay to allow controller to process
    String response = CurtisSerial.readStringUntil(';'); // Read until semicolon acknowledgment
    response.trim();
    if (response.endsWith(";")) {
      Serial.println("Curtis connection established (received ';').");
      isCurtisConnected = true;
    } else {
      Serial.print("Curtis connection failed or no ACK: '");
      Serial.print(response);
      Serial.println("'. Retrying...");
      delay(11000); // Wait before retrying connection
    }
  }

  // Only poll and send if Curtis is connected
  if (isCurtisConnected) {
    // 2. Send Polling Character (to keep session alive)
    CurtisSerial.print(POLLING_CHAR);
    delay(50); // Small delay after sending polling char

    // 3. Poll each Curtis parameter
    // Serial.println("\nPolling Curtis parameters:"); // Removed to reduce Serial spam
    for (int i = 0; i < NUM_CURTIS_PARAMS; i++) {
      String value = readCurtisParameter(curtisParameters[i].code);
      if (value.length() > 0) {
        curtisParameters[i].lastValue = value;
        // Update display if on a data screen
        if (currentScreen == DASH_SCREEN) {
          // Redraw the specific parameter on the Dash screen
          tft.setTextFont(2);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.fillRect(10, 50 + (i * 30), tft.width() - 20, 25, TFT_BLACK); // Clear old text
          tft.setCursor(10, 50 + (i * 30));
          tft.printf("%s: %s", curtisParameters[i].name, curtisParameters[i].lastValue.c_str());
        }
      } else {
        Serial.print("  Failed to read ");
        Serial.print(curtisParameters[i].name);
        Serial.println(". Connection might be unstable.");
        // Consider setting isCurtisConnected = false if multiple failures occur
      }
    }

    // 4. Prepare and Send data via ESP-NOW
    // Serial.println("Preparing ESP-NOW data for broadcast..."); // Removed to reduce Serial spam
    // Populate the outgoing ESP-NOW message struct
    strncpy(myDataTx.motorTemp, curtisParameters[0].lastValue.c_str(), sizeof(myDataTx.motorTemp) - 1);
    myDataTx.motorTemp[sizeof(myDataTx.motorTemp) - 1] = '\0';

    strncpy(myDataTx.battVolts, curtisParameters[1].lastValue.c_str(), sizeof(myDataTx.battVolts) - 1);
    myDataTx.battVolts[sizeof(myDataTx.battVolts) - 1] = '\0';

    strncpy(myDataTx.throttlePos, curtisParameters[2].lastValue.c_str(), sizeof(myDataTx.throttlePos) - 1);
    myDataTx.throttlePos[sizeof(myDataTx.throttlePos) - 1] = '\0';

    esp_err_t result = esp_now_send(peerMacAddress, (uint8_t *) &myDataTx, sizeof(myDataTx));
    if (result == ESP_OK) {
      // Serial.println("ESP-NOW: Sent data successfully."); // Removed to reduce Serial spam
    } else {
      Serial.print("ESP-NOW: Error sending data: ");
      Serial.println(result);
    }
  } else {
    // If not connected to Curtis, potentially display a message on the screen
    if (currentScreen != MAIN_MENU) { // Avoid redrawing constantly on main menu
       tft.setTextColor(TFT_RED, TFT_BLACK);
       tft.setTextFont(2);
       tft.setCursor(10, TFT_HEIGHT - 60);
       tft.println("Curtis Not Connected!");
    }
  }

  // Small delay for polling loop.
  // Touch handling needs to be responsive, so keep this delay short or use non-blocking methods.
  delay(1000); // Poll and broadcast every 1 second (was 5s, changed for faster updates)
}