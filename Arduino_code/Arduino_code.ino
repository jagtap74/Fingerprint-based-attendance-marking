#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// ---------------- WIFI ----------------
const char* ssid = "Redmi 12 5G";
const char* password = "0123456789";
String scriptURL = "https://script.google.com/macros/s/AKfycbx-VdiQIUXWGJayhNWYs4LjT3CDPGIq0FiGA3q--uB8FZb0rOjoj2FgwBOm2XchY6xA/exec";

// ---------------- SUBJECT ----------------
String subjects[] = {"PDC", "CN", "EP", "PM"};
String currentSubject = "";

// ---------------- TEACHER ----------------
int teacherStartID = 61;
int teacherEndID = 64;

// ---------------- HARDWARE ----------------
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- PINS ----------------
#define BTN1 32
#define BTN2 33
#define BTN3 25
#define BTN4 26
#define BUZZER 13
#define LED 12

// ---------------- SYSTEM ----------------
bool subjectActive = false;
int activeTeacher = -1;
bool attendanceMode = true;
int roll = 1;

unsigned long lastAttendance[101][4];
unsigned long lockTime = 3600000;

bool forceRefresh = false;
String lastLine1 = "";
String lastLine2 = "";

// ================= SETUP =================
void setup() {

  Serial.begin(115200);

  for (int i = 0; i < 101; i++) {

  for (int j = 0; j < 4; j++) {

    lastAttendance[i][j] = 0;
  }
}

  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);

  finger.begin(57600);

  if (!finger.verifyPassword()) {
    lcd.print("Sensor Error");
    while (1);
  }

  WiFi.begin(ssid, password);

  lcd.clear();
  lcd.print("Connecting...");

  while (WiFi.status() != WL_CONNECTED) delay(500);

  lcd.clear();
  lcd.print("WiFi OK");
  delay(1500);

  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
}

// ================= LOOP =================
void loop() {

  handleButtons();

  String line1, line2;

  if (!attendanceMode) {
    line1 = "ENROLL MODE";
    line2 = "ID:" + String(roll);
  }
  else if (!subjectActive) {
    line1 = "SCAN TEACHER";
    line2 = getDateTime();
  }
  else {
    line1 = "Sub:" + currentSubject;
    line2 = getDateTime();
  }

  if (forceRefresh) {
    lastLine1 = "";
    lastLine2 = "";
    forceRefresh = false;
  }

  if (line1 != lastLine1) {
    lcd.setCursor(0,0);
    lcd.print("                ");
    lcd.setCursor(0,0);
    lcd.print(line1);
    lastLine1 = line1;
  }

  if (line2 != lastLine2) {
    lcd.setCursor(0,1);
    lcd.print("                ");
    lcd.setCursor(0,1);
    lcd.print(line2);
    lastLine2 = line2;
  }
}

// ================= BUTTON =================
void handleButtons() {

  static unsigned long pressTime = 0;

  if (digitalRead(BTN4) == LOW) {
    pressTime = millis();
    while (digitalRead(BTN4) == LOW);

    if (millis() - pressTime > 1500) {
      attendanceMode = !attendanceMode;
      showMsg(attendanceMode ? "ATTEND MODE" : "ENROLL MODE");
      return;
    }
  }

  if (!attendanceMode) {

    if (digitalRead(BTN2) == LOW) {
      roll++;
      if (roll > 70) roll = 1;
      showMsg("ID:" + String(roll));
    }

    if (digitalRead(BTN3) == LOW) {
      roll--;
      if (roll < 1) roll = 70;
      showMsg("ID:" + String(roll));
    }

    if (digitalRead(BTN1) == LOW) {
      enrollFinger(roll);
    }

    if (digitalRead(BTN4) == LOW) {

  finger.deleteModel(roll);

  for (int j = 0; j < 4; j++) {

    lastAttendance[roll][j] = 0;
  }

  showMsg("DELETED");
}

    return;
  }

  handleFinger();
}

// ================= FINGER =================
void handleFinger() {

  if (digitalRead(BTN1) == LOW) {

    lcd.clear();
    lcd.print("Scan Finger");

    while (finger.getImage() != FINGERPRINT_OK);

    if (finger.image2Tz() != FINGERPRINT_OK) return;
    if (finger.fingerSearch() != FINGERPRINT_OK) {
      beepError();
      return;
    }

    int id = finger.fingerID;

    // TEACHER
    if (id >= teacherStartID && id <= teacherEndID) {

      if (subjectActive && activeTeacher == id) {
        subjectActive = false;
        activeTeacher = -1;
        showMsg("ATTEND CLOSED");
        return;
      }

      int index = id - teacherStartID;
      currentSubject = subjects[index];
      subjectActive = true;
      activeTeacher = id;

      showMsg("SUB:" + currentSubject);
      return;
    }

    // STUDENT
    if (!subjectActive) {
      showMsg("CLOSED");
      return;
    }

   unsigned long now = millis();

int subIndex = -1;

for (int i = 0; i < 4; i++) {

  if (subjects[i] == currentSubject) {

    subIndex = i;
    break;
  }
}

if (subIndex == -1) return;

if (lastAttendance[id][subIndex] != 0 &&
    now - lastAttendance[id][subIndex] < lockTime) {

  showMsg("ALREADY");
  return;
}

lastAttendance[id][subIndex] = now;

markAttendance(id);
  }
}

// ================= ENROLL =================
void enrollFinger(int id) {

  // 👉 check ID already used
  if (finger.loadModel(id) == FINGERPRINT_OK) {

    lcd.clear();
    lcd.print("ID ALREADY");
    lcd.setCursor(0,1);
    lcd.print("USED");

    beepError();
    delay(2000);
    return;
  }

  lcd.clear();
  lcd.print("Place Finger");

  // first scan
  while (finger.getImage() != FINGERPRINT_OK);

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    showMsg("IMAGE ERROR");
    return;
  }

  // 🔥 DUPLICATE CHECK
  if (finger.fingerSearch() == FINGERPRINT_OK) {

    lcd.clear();
    lcd.print("FINGER");
    lcd.setCursor(0,1);
    lcd.print("ALREADY USED");

    beepError();
    delay(2500);

    return;
  }

  lcd.clear();
  lcd.print("Remove Finger");
  delay(2000);

  while (finger.getImage() != FINGERPRINT_NOFINGER);

  lcd.clear();
  lcd.print("Place Again");

  // second scan
  while (finger.getImage() != FINGERPRINT_OK);

  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    showMsg("IMAGE ERROR");
    return;
  }

  // create model
  if (finger.createModel() != FINGERPRINT_OK) {

    lcd.clear();
    lcd.print("MATCH FAILED");

    beepError();
    delay(2000);
    return;
  }

  // store
  if (finger.storeModel(id) == FINGERPRINT_OK) {

    lcd.clear();
    lcd.print("STORED OK");
    lcd.setCursor(0,1);
    lcd.print("ID:");
    lcd.print(id);
    digitalWrite(LED, HIGH);
delay(2000);
digitalWrite(LED, LOW);

    beepSuccess();

  } else {

    lcd.clear();
    lcd.print("STORE FAILED");

    beepError();
  }

  delay(2000);
}
// ================= ATTEND =================
void markAttendance(int id) {

lcd.clear();
lcd.print("MARKING...");

if (currentSubject == "") {
showMsg("NO SUBJECT");
return;
}

struct tm t;
String date="", time="";

if (getLocalTime(&t)) {


char d[10];
sprintf(d,"%02d/%02d",t.tm_mday,t.tm_mon+1);
date = String(d);

int h = t.tm_hour;
String ap = "AM";

if(h >= 12) ap = "PM";
if(h == 0) h = 12;
else if(h > 12) h -= 12;

char tt[10];
sprintf(tt,"%02d:%02d",h,t.tm_min);

time = String(tt) + " " + ap;


} else {


date = "01-01";
time = "12:00 AM";


}

date.replace("/", "-");

String timeSafe = time;
timeSafe.replace(" ", "%20");

String url = scriptURL + "?id=" + String(id) +
"&subject=" + currentSubject +
"&date=" + date +
"&time=" + timeSafe;

Serial.println(url);

if (WiFi.status() == WL_CONNECTED) {


HTTPClient http;

http.begin(url);

http.setTimeout(1000);

http.GET();

http.end();

lcd.clear();
lcd.print("Attendance");
lcd.setCursor(0,1);
lcd.print("Marked");
lcd.print("ID:" + String(id));

digitalWrite(LED, HIGH);
delay(300);
digitalWrite(LED, LOW);

beepSuccess();

delay(1500);

forceRefresh = true;


}
else {


lcd.clear();
lcd.print("WiFi Error");

beepError();


}
}

// ================= UTILS =================
void showMsg(String msg){
  lcd.clear();
  lcd.print(msg);
  delay(1000);
  forceRefresh = true;
}

void beepSuccess() {
  tone(BUZZER, 1000, 150);   // short beep
}

void beepError() {
  tone(BUZZER, 500, 300);    // long beep
}

// ================= TIME =================
String getDateTime(){
  struct tm t;
  if(getLocalTime(&t)){
    int h=t.tm_hour;
    String ap="AM";
    if(h>=12) ap="PM";
    if(h==0) h=12;
    else if(h>12) h-=12;

    char buf[25];
    sprintf(buf,"%02d/%02d %02d:%02d %s",
            t.tm_mday,t.tm_mon+1,h,t.tm_min,ap.c_str());
    return String(buf);
  }
  return "--/-- --:--";
}