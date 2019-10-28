/*
In this example, we will be reading data logs of an inbuilt function called millis(), which returns the number of milliseconds passed since the ESP board began 
running the current program, to our phones using the RF430 board. The data from the function is stored in a Serial Peripheral Interface Flash File System until 
the user presses a button. As soon as the button is pressed, the NFC tag reads the contents of the file, prepares a message and transfers it to a nearby NFC reader
 (in this case our phone). On this transfer is complete, the existing file is erased and fresh data begins to be logged.
*/
#include "FS.h"
#include "SPIFFS.h"
#include <Wire.h>
#include <RF430CL.h>
#include <NDEF.h>
#include <NDEF_TXT.h>

#define FORMAT_SPIFFS_IF_FAILED true
#define RF430CL330H_BOOSTERPACK_RESET_PIN 13 //Reset pin connected to ESP32
#define RF430CL330H_BOOSTERPACK_IRQ_PIN 14   //Interrupt pin connected to ESP32

#define UPDATE_DELAY 2000 //frequency at which SPIFFS needs to be updated
volatile boolean update_trigger = false;

RF430 nfc(RF430CL330H_BOOSTERPACK_RESET_PIN, RF430CL330H_BOOSTERPACK_IRQ_PIN);

NDEF_TXT txt("en");
char txtbuf[2048];

uint32_t lastUpdate = millis();

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root)
    {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels)
            {
                listDir(fs, file.name(), levels - 1);
            }
        }
        else
        {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

String readFile(fs::FS &fs, const char *path)
{
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);

    Serial.println("- read from file:");
    while (file.available())
    {
        String result;
        for (int j = 0; j < file.size(); j++)
        {
            result += ((char)file.read());
        }
        Serial.println(result);
        return result;
    }
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- file written");
    }
    else
    {
        Serial.println("- frite failed");
    }
}

void appendFile(fs::FS &fs, const char *path, const char *message)
{
    //Serial.printf("Appending to file: %s\r\n", path);

    File file = fs.open(path, FILE_APPEND);
    if (!file)
    {
        Serial.println("- failed to open file for appending");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- message appended");
    }
    else
    {
        Serial.println("- append failed");
    }
}

void deleteFile(fs::FS &fs, const char *path)
{
    Serial.printf("Deleting file: %s\r\n", path);
    if (fs.remove(path))
    {
        Serial.println("- file deleted");
    }
    else
    {
        Serial.println("- delete failed");
    }
}

void triggerTextUpdate()
{
    update_trigger = true;
}

void setup()
{
    Serial.begin(115200);
    if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
    {
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    //Initializing I2C
    Wire.begin();

    //Initializing NFC Tag
    nfc.begin();

    txt.setPayloadBuffer(txtbuf, 2048); /* <-- Payload buffer contents always get NUL-terminated, so be sure
                                      * your allocated buffer is 1 byte longer than the stated maximum size.
                                      */

    //Setting & appending values to buffer
    uint32_t tlen = txt.setText("Millis Data \n");

    //Writing Text object to NFC transceiver
    int ndef_size = txt.sendTo(nfc); // Export Text NDEF object to RF430's SRAM

    nfc.setDataLength(ndef_size);

    //Activating NFC transceiver
    nfc.enable();

    //Configuring PUSH BUTTON to update NFC text record when pressed
    pinMode(17, INPUT_PULLUP);
    attachInterrupt(17, triggerTextUpdate, FALLING);

    listDir(SPIFFS, "/", 0);
    deleteFile(SPIFFS, "/readings.txt");                      //It is important to delete any existing files by the same name before creating a new one
    writeFile(SPIFFS, "/readings.txt", " Millis Readings: "); //Creating a new file readings.txt
    Serial.println("Setup successful");
}

void loop()
{
    if ((millis() - lastUpdate) > UPDATE_DELAY)
    {
        lastUpdate = millis();
        String data = String(millis());
        data += '\n';
        char buff[100];
        data.toCharArray(buff, 100);
        Serial.println(buff);
        appendFile(SPIFFS, "/readings.txt", buff);
    }

    if (update_trigger)
    {
        //char *res = {};
        String res = readFile(SPIFFS, "/readings.txt");
        txtbuf[0] = '\0';
        txt.print(res);
        nfc.disable();
        nfc.setDataPointer(0);
        size_t ndef_size = txt.sendTo(nfc);
        Serial.println(ndef_size);
        nfc.setDataLength(ndef_size);
        nfc.enable();
        Serial.println("Tag updated.");

        update_trigger = false; // Detrigger
    }

    if (nfc.loop())
    {
        if (nfc.wasRead())
        {
            Serial.println("NDEF tag was read!");
            ESP.restart(); //restart the ESP so that existing file is removed and new data is logged fresh in a new file
        }
        if (nfc.available())
        {
            Serial.print("NFC master has written a new tag! ");
            uint16_t len = nfc.getDataLength();
            Serial.print(len);
            Serial.println(" bytes");
            nfc.flush(); // prevent nfc.available() from returning true again
        }
        nfc.enable();
    }
}
