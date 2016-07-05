#include "badge.h"
#include "stdint.h"
#include "gui.h"
#include "menus.h"
#include <RFM69.h>
#include <Keyboard.h>
#include <KeyStore.h>
#include <tim.h>
#include <usart.h>

char sendingBuf[64] = { '\0' };
char receivingBuf[64] = { '\0' };

#define ONE_TIME 1

int state = 0;
int tmp = 0;

//RH_RF69 Radio(RFM69_SPI_NSS_Pin,RFM69_Interrupt_DIO0_Pin);
QKeyboard KB(QKeyboard::PinConfig(KEYBOARD_Y1_GPIO_Port, KEYBOARD_Y1_Pin),
		QKeyboard::PinConfig(KEYBOARD_Y2_GPIO_Port, KEYBOARD_Y2_Pin),
		QKeyboard::PinConfig(KEYBOARD_Y3_GPIO_Port, KEYBOARD_Y3_Pin),
		QKeyboard::PinConfig(KEYBOARD_X1_GPIO_Port, KEYBOARD_X1_Pin),
		QKeyboard::PinConfig(KEYBOARD_X2_GPIO_Port, KEYBOARD_X2_Pin),
		QKeyboard::PinConfig(KEYBOARD_X3_GPIO_Port, KEYBOARD_X3_Pin),
		QKeyboard::PinConfig(KEYBOARD_X4_GPIO_Port, KEYBOARD_X4_Pin));

//RH_RF69 Radio(RFM69_SPI_NSS_Pin, RFM69_Interrupt_DIO0_Pin);
RFM69 Radio;

//start at 45K for 19K
//start at 55K for 10
static const uint32_t START_STORAGE_LOCATION = 0x800d400; //0x800B000;
ContactStore MyContacts(START_STORAGE_LOCATION, 0x2710); //0x4E00);

ContactStore &getContactStore() {
	return MyContacts;
}

RFM69 &getRadio() {
	return Radio;
}

void delay(uint32_t time) {
	HAL_Delay(time);
}


const char *ErrorType::getMessage() {
	return "TODO";
}

uint32_t nextStateSwitchTime = 0;

void initFlash() {
#if ONE_TIME==1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"

	HAL_FLASH_Unlock();
	uint16_t loc = 0;
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, START_STORAGE_LOCATION, 0xDCDC);
	loc += 2;
	static const unsigned int defaults1 = 0b00100001; //screen saver type = 1 sleep time = 2
	static const unsigned int defaults2 = 0b00000001; //screen saver time = 1
	unsigned char reserveFlags = 0; // makeUber == 1 ? 0x1 : 0x0;
	uint16_t ReserveContacts = ((reserveFlags) << 8) | 0x0;
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, START_STORAGE_LOCATION + loc, ReserveContacts);
	uint16_t Settings = (defaults1 << 8) | defaults2;
	loc += 2;
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, START_STORAGE_LOCATION + loc, Settings);
	loc += 2;
	uint8_t RadioID[4] = { 0x1, 0x2, 0x3, 0x4 };
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, START_STORAGE_LOCATION + loc, *((uint32_t*) &RadioID[0]));
	loc += 4;
	uint8_t pubKey[ContactStore::PUBLIC_KEY_LENGTH] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 1, 0 };
	for (int i = 0; i < ContactStore::PUBLIC_KEY_LENGTH; i++, loc += 2) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, START_STORAGE_LOCATION + loc, *((uint16_t*) &pubKey[i]));
	}
	uint8_t privateKey[ContactStore::PRIVATE_KEY_LENGTH] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2 };
	for (int i = 0; i < ContactStore::PRIVATE_KEY_LENGTH; i++, loc += 2) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, START_STORAGE_LOCATION + loc, *((uint16_t*) &privateKey[i]));
	}
	uint8_t agentName[ContactStore::AGENT_NAME_LENGTH] = { 0x0 };
	for (int i = 0; i < ContactStore::AGENT_NAME_LENGTH; i++, loc += 2) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, START_STORAGE_LOCATION + loc, *((uint16_t*) &agentName[i]));
	}

	HAL_FLASH_Lock();
#pragma GCC diagnostic pop
#endif
}

StateBase *CurrentState = 0;

uint32_t startBadge() {
	uint32_t retVal = 0;
	initFlash();

	GUI_ListItemData items[4];
	GUI_ListData DrawList((const char *) "Self Check", (GUI_ListItemData**) items, uint8_t(0), uint8_t(0), uint8_t(128),
			uint8_t(64), uint8_t(0), uint8_t(0));
	//DO SELF CHECK
	if (gui_init()) {
		items[0].set(0, "OLED_INIT");
		DrawList.ItemsCount++;
		retVal |= COMPONENTS_ITEMS::OLED;
		gui_set_curList(&DrawList);
	}
	gui_draw();
#if 1
#define INITIAL_STATE 6
	if (Radio.initialize(RF69_915MHZ, 1)) {
#elif 0
#define INITIAL_STATE 7
		if(Radio.initialize(RF69_915MHZ, 2)) {
#else
#define INITIAL_STATE 1
#endif
		items[1].set(1, "RADIO INIT");
		Radio.setPowerLevel(31);
		retVal |= COMPONENTS_ITEMS::RADIO;
	} else {
		items[1].set(1, "RADIO FAILED");
	}
	DrawList.ItemsCount++;
	gui_draw();

	if (MyContacts.init()) {
		items[2].set(2, "Flash mem INIT");
		retVal |= COMPONENTS_ITEMS::FLASH_MEM;
	} else {
		items[2].set(2, "Flash mem FAILED");
	}
	//test for IR??
	DrawList.ItemsCount++;
	gui_draw();

	//remove nextStateswitch
	nextStateSwitchTime = HAL_GetTick() + 5000;
	//move to use of IR
	//initUARTIR();

	////////
	state = INITIAL_STATE;
	gui_set_curList(0);
	CurrentState = StateFactory::getLogoState(3000);
	return true;
}

int counter = 0;

void checkStateTimer(int nextState, int timeToNextSwitch) {
	if (nextStateSwitchTime < HAL_GetTick()) {
		state = nextState;
		nextStateSwitchTime = HAL_GetTick() + timeToNextSwitch;
	}
}

uint32_t lastSendTime = 0;


void loopBadge() {

	ReturnStateContext rsc = CurrentState->run(KB);

	if (rsc.Err.ok()) {
		CurrentState = rsc.NextMenuToRun;
	}
	gui_draw();

	/*

	 char buf[128];

	 //gui_text(&sendingBuf[0],0,20,0);
	 //gui_text(&receivingBuf[0],0,30,0);
	 KB.scan();

	 switch (state) {
	 case 0: {
	 checkStateTimer(1, 2000);
	 }
	 break;
	 case 1: // default deplay
	 {
	 sprintf(&buf[0], "UID: %d\n%s\n%s",
	 MyContacts.getMyInfo().getUniqueID(),
	 MyContacts.getMyInfo().getAgentName(), "HPUB");
	 gui_lable_multiline(&buf[0], 0, 10, 120, 50, SSD1306_COLOR_BLACK, 0);
	 checkStateTimer(2, 10000);
	 }
	 break;
	 case 2: {
	 sprintf(&buf[0], "Key Pushed: %d\nKey Sel:%d", KB.getLastPinPushed(),
	 KB.getLastPinSeleted());
	 gui_lable_multiline(&buf[0], 0, 10, 120, 50, SSD1306_COLOR_BLACK, 0);
	 checkStateTimer(2, 10000);
	 }
	 break;
	 case 5: {
	 sprintf(sendingBuf, "Case 0:");
	 if (nextStateSwitchTime == 0) {
	 nextStateSwitchTime = HAL_GetTick() + 2000;
	 }
	 if (nextStateSwitchTime < HAL_GetTick()) {
	 state = 1;
	 }
	 }
	 break;
	 case 6:
	 //HAL_NVIC_EnableIRQ(EXTI0_IRQn);
	 if (HAL_GetTick() - lastSendTime > 1000) {
	 lastSendTime = HAL_GetTick();
	 sprintf(sendingBuf, "Sending 'Hi for the %d time'", counter++);
	 //Radio.send((const uint8_t*)&sendingBuf[0],strlen(sendingBuf));
	 Radio.send(2, sendingBuf, strlen(sendingBuf), true);

	 gui_lable_multiline(sendingBuf, 0, 10, 120, 50, SSD1306_COLOR_BLACK,
	 0);
	 }
	 nextStateSwitchTime = HAL_GetTick() + 10000;
	 HAL_Delay(1000);
	 state = 6;
	 break;
	 case 7: {
	 if (HAL_GetTick() - lastSendTime > 1000) {
	 sprintf(receivingBuf, "Receiving for %d time", counter++);
	 lastSendTime = HAL_GetTick();
	 gui_lable_multiline(receivingBuf, 0, 20, 120, 50,
	 SSD1306_COLOR_BLACK, 0);
	 if (Radio.receiveDone()) {
	 memset(&receivingBuf[0], 0, sizeof(receivingBuf));
	 //Radio.recv((uint8_t*)&receivingBuf[0],&bufSize);
	 gui_lable_multiline(receivingBuf, 0, 30, 120, 50,
	 SSD1306_COLOR_BLACK, 0);
	 nextStateSwitchTime = HAL_GetTick() + 4000;
	 state = 7;
	 } else {
	 if (nextStateSwitchTime < HAL_GetTick()) {
	 state = 7;
	 nextStateSwitchTime = HAL_GetTick() + 2000;
	 gui_lable_multiline("timeout", 0, 30, 120, 50,
	 SSD1306_COLOR_BLACK, 0);
	 }
	 }
	 HAL_Delay(1000);
	 }
	 }
	 break;
	 case 8: {
	 gui_lable_multiline("receiver", 0, 10, 120, 50, SSD1306_COLOR_BLACK, 0);
	 HAL_StatusTypeDef status;
	 char receiveBuf[64] = { '\0' };
	 status = HAL_UART_Receive(&huart2, (uint8_t *) &receiveBuf[0],
	 sizeof(receiveBuf) - 1, 5000);
	 if (status == HAL_OK
	 || (status == HAL_TIMEOUT
	 && huart2.RxXferCount != huart2.RxXferSize)) {
	 //Error_Handler();
	 sprintf(&buf[0], "UART:  %s", &receiveBuf[0]);
	 gui_lable_multiline(&buf[0], 0, 30, 120, 50, SSD1306_COLOR_BLACK,
	 0);
	 }
	 }
	 break;
	 case 9: {
	 gui_lable_multiline("transmitter", 0, 10, 120, 50, SSD1306_COLOR_BLACK,
	 0);
	 if (KB.getLastPinSeleted() != QKeyboard::NO_PIN_SELECTED) {
	 state = 9;
	 }
	 char transbuf[64] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	 //strcpy(&transbuf[0], "AAAAAAAA");
	 HAL_StatusTypeDef status;
	 status = HAL_UART_Transmit(&huart2, (uint8_t*) &transbuf[0], 10, 5000);
	 if (HAL_OK == status) {
	 gui_lable_multiline("sent ok", 0, 10, 120, 50, SSD1306_COLOR_BLACK,
	 0);
	 }
	 delay(500);

	 }
	 break;
	 case 10: {
	 HAL_GetDEVID();
	 HAL_GetREVID();
	 HAL_GetHalVersion();
	 }
	 break;
	 case 11: {
	 Radio.getCurrentGain();
	 Radio.getFrequency();
	 Radio.getImpedenceLevel();

	 *
	 * uint32_t f = Radio.getFrequency();
	 sprintf(&buf[0], "Fre:  %d", f);
	 gui_text(&buf[0], 0, 20, 0);
	 //Radio.readAllRegs();
	 sprintf(&buf[0], "rssi: %d", Radio.readRSSI(false));


	 }
	 break;
	 }
	 */
	//const char * str = "HI STM32";
	//gui_text(str,10,10,SSD1306_COLOR_WHITE);
	//gui_draw();
}

