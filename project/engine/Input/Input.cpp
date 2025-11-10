#include "Input.h"
#include "WinApp.h" 
#include <cassert>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")


Input::Input() {
	
}


Input::~Input() {

	Finalize();
}

void Input::Initialize(WinApp* winApp) {
	HRESULT hr;

	
	hr = DirectInput8Create(
		winApp->GetHInstance(),
		DIRECTINPUT_VERSION,
		IID_IDirectInput8,
		(void**)&directInput_,
		nullptr);
	assert(SUCCEEDED(hr));

	
	hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, nullptr);
	assert(SUCCEEDED(hr));

	
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	
	hr = keyboard_->SetCooperativeLevel(
		winApp->GetHwnd(), 
		DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr));
}


void Input::Finalize() {
	if (keyboard_) {
		keyboard_->Unacquire(); 
	}
	keyboard_.Reset();
	directInput_.Reset();
}

void Input::Update() {
	
	std::memcpy(prevKeys_, keys_, sizeof(keys_));

	
	HRESULT hr = keyboard_->Acquire();

	
	if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
		hr = keyboard_->Acquire();
		if (FAILED(hr)) {
			
			return;
		}
	}


	hr = keyboard_->GetDeviceState(sizeof(keys_), keys_);
	if (FAILED(hr)) {
		
		return;
		
	}
}

bool Input::IsKeyPressed(uint8_t keyCode) {
	
	return (keys_[keyCode] & 0x80);
}

bool Input::IsKeyTriggered(uint8_t keyCode) {
	
	return (keys_[keyCode] & 0x80) && !(prevKeys_[keyCode] & 0x80);
}


bool Input::IsKeyReleased(uint8_t keyCode) {
	
	return !(keys_[keyCode] & 0x80) && (prevKeys_[keyCode] & 0x80);
}