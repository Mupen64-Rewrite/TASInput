/*
Copyright (C) 2001 Deflection

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/*
	Modified for TAS by Nitsuja
*/


#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include "Controller.h"
#include "DI.h"
#include "DefDI.h"
#include "Config.h"
#include "resource.h"
#include "Combo.h"

#ifdef DEBUG
#define PLUGIN_NAME "TAS Input debug"
#else
#define PLUGIN_NAME "TAS Input"
#endif

#define PI 3.14159265358979f
#define BUFFER_CHUNK 128

int MOUSE_LBUTTONREDEFINITION = VK_LBUTTON;
int MOUSE_RBUTTONREDEFINITION = VK_RBUTTON;

#undef List // look at line 32 for cause

HINSTANCE g_hInstance;

GUID Guids[MAX_DEVICES];
DEFCONTROLLER Controller[NUMBER_OF_CONTROLS];
CONTROL* ControlDef[NUMBER_OF_CONTROLS];
//LRESULT CALLBACK StatusDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK StatusDlgProc0(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK StatusDlgProc1(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK StatusDlgProc2(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK StatusDlgProc3(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
DWORD WINAPI StatusDlgThreadProc(LPVOID lpParameter);
bool romIsOpen = false;
HMENU hMenu;

HANDLE fakeStatusThread = NULL; // fake! used for testing plugin
bool validatedhTxtbox = FALSE;
UINT systemDPI;

bool lock; //don't focus mupen

std::vector<Combo*> combos;
#define ACTIVE_COMBO combos[activeCombo]

MENUCONFIG menuConfig;
HWND textXHWND = NULL, textYHWND = textXHWND;


struct Status
{
    Status()
    {
        statusThread = NULL;
        dwThreadId = 0;
        copyButtons = false;
        overrideOn = false;
        overrideAllowed = false;
        relativeControlNow = false;
        gettingKeys = false;
        relativeXOn = 0;
        relativeYOn = 0;
        radialAngle = -PI / 2;
        skipEditX = false;
        skipEditY = false;
        xScale = 1.0f;
        yScale = 1.0f;
        //		frameCounter = 0;
        buttonOverride.Value = 0;
        buttonAutofire.Value = 0;
        buttonAutofire2.Value = 0;
        buttonDisplayed.Value = 0;
        radialDistance = 0.0f;
        radialRecalc = true;
        statusDlg = NULL;
        prevHWnd = NULL;
        Extend = 0;
        positioned = false;
        comboTask = C_IDLE;
        once = true;
    }

    void StartThread(int ControllerNumber)
    {
        HANDLE prevStatusThread = statusThread;
        HWND prevStatusDlg = statusDlg;

        once = true; //redraw once

        Control = ControllerNumber;
        dwThreadId = Control;
        dwThreadParam = MAKEWORD(Control, Extend);

        // CreateDialog() won't work, because the emulator eats our messages when it's paused
        // and can't call IsDialogMessage() because it doesn't know what our dialog is.
        // So, create a new thread and spawn a MODAL (to that thread) dialog,
        // to guarantee it always gets the messages it should.
        statusThread = CreateThread(0, 0, StatusDlgThreadProc, &dwThreadParam, 0, &dwThreadId);

        if (prevStatusDlg)
            DestroyWindow(prevStatusDlg);
        if (prevStatusThread)
            TerminateThread(prevStatusThread, 0); // last because prevStatusThread might be the currently-running thread
    }

    void StopThread()
    {
        if (statusDlg)
        {
            DestroyWindow(statusDlg);
            statusDlg = NULL;
        }
        if (statusThread)
        {
            FreeCombos(); //apparently this is a good idea
            TerminateThread(statusThread, 0);
            statusThread = NULL;
        }
    }

    void EnsureRunning()
    {
        if (!statusDlg || !statusThread || !initialized)
        {
            if (statusDlg) DestroyWindow(statusDlg), statusDlg = NULL;
            if (statusThread) TerminateThread(statusThread, 0), statusThread = NULL;
            StartThread(Control);
        }
    }

    HANDLE statusThread;
    DWORD dwThreadId, dwThreadParam;
    int copyButtons;
    int overrideX, overrideY;
    bool overrideOn, overrideAllowed, relativeControlNow, gettingKeys;
    //	bool incrementingFrameNow;
    DWORD relativeXOn, relativeYOn;
    float radialAngle, radialDistance, radialRecalc;
    bool dragging, draggingStick, draggingPermaStick;
    bool AngDisp;
    int dragXStart, dragYStart;
    int lastXDrag, lastYDrag;
    bool deactivateAfterClick, skipEditX, skipEditY;
    bool positioned, initialized;
    int xPosition, yPosition;
    float xScale, yScale;
    //	int frameCounter;
    static int frameCounter;
    int comboStart;
    BUTTONS buttonOverride, buttonAutofire, buttonAutofire2;
    BUTTONS buttonDisplayed;
    BUTTONS LastControllerInput;
    BUTTONS LastPureControllerInput; //without overrides/combo
    HWND statusDlg;
    HWND prevHWnd;
    HWND lBox;
    int Control;
    int Extend;
    int comboTask;
    int activeCombo;
    bool fakeInput;

    bool once;

    void FreeCombos();

    void set_status(std::string str);
    void SaveCombos();
    void load_combos(const char*);
    int CreateNewCombo(int);
    void StartEdit(int);
    void EndEdit(int, char*);

    void RefreshAnalogPicture();

    void ActivateEmulatorWindow();
    bool IsWindowFromEmulatorProcessActive();
    static bool IsAnyStatusDialogActive();
    LRESULT StatusDlgMethod(UINT msg, WPARAM wParam, LPARAM lParam);
    void UpdateVisuals(BUTTONS ControllerInput);
    void GetKeys(BUTTONS* Keys);
    void SetKeys(BUTTONS ControllerInput);
};

int Status::frameCounter = 0;


Status status[NUMBER_OF_CONTROLS];

int WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, PVOID pvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        g_hInstance = hInstance;
        break;

    case DLL_PROCESS_DETACH:
        FreeDirectInput();
        break;
    }

    // HACK: perform windows left handed mode check 
    // and adjust accordingly
    if (GetSystemMetrics(SM_SWAPBUTTON))
    {
        MOUSE_LBUTTONREDEFINITION = VK_RBUTTON;
        MOUSE_RBUTTONREDEFINITION = VK_LBUTTON;
    }

    return TRUE;
}

EXPORT void CALL CloseDLL(void)
{
    //Stop and Close Direct Input
    FreeDirectInput();
}

EXPORT void CALL ControllerCommand(int Control, BYTE* Command)
{
}

EXPORT void CALL DllAbout(HWND hParent)
{
    if (MessageBox(
        hParent,
        PLUGIN_NAME
        "\nFor DirectX 7 or higher\nBased on Def's Direct Input 0.54 by Deflection\nTAS Modifications by Nitsuja\nContinued development by the Mupen64-rr-lua contributors.\nDo you want to visit the repository?",
        "About", MB_ICONINFORMATION | MB_YESNO) == IDYES)
        ShellExecute(0, 0, "https://github.com/mkdasher/mupen64-rr-lua-/tree/dev/tasinput_plugin/src", 0, 0, SW_SHOW);
}

EXPORT void CALL DllConfig(HWND hParent)
{
    if (g_lpDI == NULL)
        InitializeAndCheckDevices(hParent);
    else
        DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_CONFIGDLG), hParent, (DLGPROC)ConfigDlgProc);

    // make config box restart the input status control if it's been closed
    if (romIsOpen)
        for (int i = 0; i < NUMBER_OF_CONTROLS; i++)
            if (Controller[i].bActive)
                status[i].EnsureRunning();
            else
                status[i].StopThread();
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO* PluginInfo)
{
    PluginInfo->Version = 0x0100;
    PluginInfo->Type = PLUGIN_TYPE_CONTROLLER;
    wsprintf(PluginInfo->Name,PLUGIN_NAME);
}

EXPORT void CALL GetKeys(int Control, BUTTONS* Keys)
{
    if (Control >= 0 && Control < NUMBER_OF_CONTROLS && Controller[Control].bActive)
        status[Control].GetKeys(Keys);
    else
        Keys->Value = 0;
}

EXPORT void CALL SetKeys(int Control, BUTTONS ControllerInput)
{
    if (Control >= 0 && Control < NUMBER_OF_CONTROLS && Controller[Control].bActive)
        status[Control].SetKeys(ControllerInput);
}

//check if given combo data uses joystick
bool ParseCombo(BUTTONS* data, int len)
{
    for (int i = 0; i != len; i++)
    {
        if (data[i].X_AXIS || data[i].Y_AXIS) return true;
    }
    return false;
}

LRESULT CALLBACK EditBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR sId, DWORD_PTR dwRefData)
{
    switch (msg)
    {
    case WM_GETDLGCODE:
        {
            char txt[MAX_PATH] = "\0";
            if (wParam == VK_RETURN)
            {
                SendMessage(hwnd, WM_GETTEXT, sizeof(txt), (LPARAM)txt);
                SendMessage(GetParent(GetParent(hwnd)), EDIT_END, 0, (LPARAM)txt);
                DestroyWindow(hwnd);
            }
            else if (wParam == VK_ESCAPE)
            {
                DestroyWindow(hwnd);
            }
            break;
        }
    case WM_KILLFOCUS: {
        char txt[MAX_PATH] = { 0 };
        SendMessage(hwnd, WM_GETTEXT, sizeof(txt), (LPARAM)txt);
        SendMessage(GetParent(GetParent(hwnd)), EDIT_END, 0, (LPARAM)txt);
        DestroyWindow(hwnd);
        lock = false;
        break;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, EditBoxProc, sId);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

//reallocate buffer to closest multiple of chunk size <- assumes it didn't overflow already
//returns pointer to new buffer (or old extended pointer)
BUTTONS* ExtendBuffer(BUTTONS* buf, int curSize) //size in bytes
{
    //realloc(buf, curSize + BUFFER_CHUNK); //bad because crashes on unaligned buffers, which may come from combo file
    int newsize = (div(curSize, BUFFER_CHUNK).quot + 1) * BUFFER_CHUNK * 4;
    return (BUTTONS*)realloc(buf, newsize);
}

void Status::GetKeys(BUTTONS* Keys)
{
    gettingKeys = true;

    //	if(incrementingFrameNow)
    //		frameCounter++;

    BUTTONS ControllerInput;

    //Empty Keyboard Button Info 
    ControllerInput.Value = 0;

    if (Controller[Control].bActive == TRUE)
    {
        BYTE buffer[256]; //Keyboard Info 
        DIJOYSTATE js; //Joystick Info
        HRESULT hr;
        int M1Speed = 0, M2Speed = 0;
        bool analogKey = false;

        if (Keys == NULL)
        {
            gettingKeys = false;
            SetKeys(ControllerInput);
            return;
        }

        for (BYTE devicecount = 0; devicecount < Controller[Control].NDevices; devicecount++)
        {
            BYTE DeviceNum = (BYTE)Controller[Control].Devices[devicecount];
            if (DInputDev[DeviceNum].lpDIDevice != NULL)
            {
                LONG count;

                if ((DInputDev[DeviceNum].DIDevInst.dwDevType & DI8DEVTYPE_KEYBOARD) == DI8DEVTYPE_KEYBOARD)
                {
                    ZeroMemory(&buffer, sizeof(buffer));
                    if FAILED(hr = DInputDev[DeviceNum].lpDIDevice->GetDeviceState(sizeof(buffer),&buffer))
                    {
                        hr = DInputDev[DeviceNum].lpDIDevice->Acquire();
                        while (hr == DIERR_INPUTLOST)
                            hr = DInputDev[DeviceNum].lpDIDevice->Acquire();
                        gettingKeys = false;
                        return;
                    }

                    for (count = 0; count < NUMBER_OF_BUTTONS; count++)
                    {
                        if (Controller[Control].Input[count].Device == DeviceNum)
                        {
                            switch (Controller[Control].Input[count].type)
                            {
                            //Record Keyboard Button Info from Device State Buffer
                            case INPUT_TYPE_KEY_BUT:
                                if (BUTTONDOWN(buffer, Controller[Control].Input[count].vkey))
                                {
                                    switch (count)
                                    {
                                    case 18:
                                        M1Speed = Controller[Control].Input[count].button;
                                        break;

                                    case 19:
                                        M2Speed = Controller[Control].Input[count].button;
                                        break;

                                    case 0:
                                    case 1:
                                    case 2:
                                    case 3:
                                        analogKey = true;
                                    /* fall through */
                                    default:
                                        ControllerInput.Value |= Controller[Control].Input[count].button;
                                        break;
                                    }
                                }
                                break;

                            default:
                                break;
                            }
                        }
                    }
                }

                else if ((DInputDev[DeviceNum].DIDevInst.dwDevType & DI8DEVTYPE_JOYSTICK) == DI8DEVTYPE_JOYSTICK)
                {
                    if FAILED(hr = DInputDev[DeviceNum].lpDIDevice->Poll())
                    {
                        hr = DInputDev[DeviceNum].lpDIDevice->Acquire();
                        while (hr == DIERR_INPUTLOST)
                            hr = DInputDev[DeviceNum].lpDIDevice->Acquire();
                        gettingKeys = false;
                        return;
                    }
                    if FAILED(hr = DInputDev[DeviceNum].lpDIDevice->GetDeviceState( sizeof(DIJOYSTATE), &js ))
                    {
                        gettingKeys = false;
                        return;
                    }

                    for (count = 0; count < NUMBER_OF_BUTTONS; count++)
                    {
                        if (Controller[Control].Input[count].Device == DeviceNum)
                        {
                            BYTE count2;
                            switch (Controller[Control].Input[count].type)
                            {
                            //Get Joystick button Info from Device State js stucture
                            case INPUT_TYPE_JOY_BUT:
                                if (BUTTONDOWN(js.rgbButtons, Controller[Control].Input[count].vkey))
                                {
                                    switch (count)
                                    {
                                    case 18:
                                        M1Speed = Controller[Control].Input[count].button;
                                        break;

                                    case 19:
                                        M2Speed = Controller[Control].Input[count].button;
                                        break;

                                    case 0:
                                    case 1:
                                    case 2:
                                    case 3:
                                        analogKey = true;
                                    /* fall through */
                                    default:
                                        ControllerInput.Value |= Controller[Control].Input[count].button;
                                        break;
                                    }
                                }
                                break;

                            case INPUT_TYPE_JOY_AXIS:
                                switch (Controller[Control].Input[count].vkey)
                                {
                                case DIJOFS_YN:
                                    if (js.lY < (LONG)-Controller[Control].SensMin)
                                        GetNegAxisVal(js.lY, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_YP:
                                    if (js.lY > (LONG)Controller[Control].SensMin)
                                        GetPosAxisVal(js.lY, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_XN:
                                    if (js.lX < (LONG)-Controller[Control].SensMin)
                                        GetNegAxisVal(js.lX, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_XP:
                                    if (js.lX > (LONG)Controller[Control].SensMin)
                                        GetPosAxisVal(js.lX, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_ZN:
                                    if (js.lZ < (LONG)-Controller[Control].SensMin)
                                        GetNegAxisVal(js.lZ, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_ZP:
                                    if (js.lZ > (LONG)Controller[Control].SensMin)
                                        GetPosAxisVal(js.lZ, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_RYN:
                                    if (js.lRy < (LONG)-Controller[Control].SensMin)
                                        GetNegAxisVal(js.lRy, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_RYP:
                                    if (js.lRy > (LONG)Controller[Control].SensMin)
                                        GetPosAxisVal(js.lRy, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_RXN:
                                    if (js.lRx < (LONG)-Controller[Control].SensMin)
                                        GetNegAxisVal(js.lRx, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_RXP:
                                    if (js.lRx > (LONG)Controller[Control].SensMin)
                                        GetPosAxisVal(js.lRx, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_RZN:
                                    if (js.lRz < (LONG)-Controller[Control].SensMin)
                                        GetNegAxisVal(js.lRz, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_RZP:
                                    if (js.lRz > (LONG)Controller[Control].SensMin)
                                        GetPosAxisVal(js.lRz, Control, count, &ControllerInput, M1Speed, M2Speed);
                                    break;
                                case DIJOFS_SLIDER0N:
                                    if (js.rglSlider[0] < (LONG)-Controller[Control].SensMin)
                                        GetNegAxisVal(js.rglSlider[0], Control, count, &ControllerInput, M1Speed,
                                                      M2Speed);
                                    break;
                                case DIJOFS_SLIDER0P:
                                    if (js.rglSlider[0] > (LONG)Controller[Control].SensMin)
                                        GetPosAxisVal(js.rglSlider[0], Control, count, &ControllerInput, M1Speed,
                                                      M2Speed);
                                    break;
                                case DIJOFS_SLIDER1N:
                                    if (js.rglSlider[1] < (LONG)-Controller[Control].SensMin)
                                        GetNegAxisVal(js.rglSlider[1], Control, count, &ControllerInput, M1Speed,
                                                      M2Speed);
                                    break;
                                case DIJOFS_SLIDER1P:
                                    if (js.rglSlider[1] > (LONG)Controller[Control].SensMin)
                                        GetPosAxisVal(js.rglSlider[1], Control, count, &ControllerInput, M1Speed,
                                                      M2Speed);
                                    break;
                                }
                                break;

                            case INPUT_TYPE_JOY_POV:
                                for (count2 = 0; count2 < NUMBER_OF_CONTROLS; count2++)
                                {
                                    if ((js.rgdwPOV[count2] != -1) && (LOWORD(js.rgdwPOV[count2]) != 0xFFFF))
                                    {
                                        switch (Controller[Control].Input[count].vkey)
                                        {
                                        case DIJOFS_POV0N:
                                        case DIJOFS_POV1N:
                                        case DIJOFS_POV2N:
                                        case DIJOFS_POV3N:
                                            if ((js.rgdwPOV[count2] >= 31500) || (js.rgdwPOV[count2] <= 4500))
                                            {
                                                switch (count)
                                                {
                                                case 18:
                                                    M1Speed = Controller[Control].Input[count].button;
                                                    break;

                                                case 19:
                                                    M2Speed = Controller[Control].Input[count].button;
                                                    break;

                                                case 0:
                                                case 1:
                                                case 2:
                                                case 3:
                                                    analogKey = true;
                                                /* fall through */
                                                default:
                                                    ControllerInput.Value |= Controller[Control].Input[count].button;
                                                    break;
                                                }
                                            }
                                            break;
                                        case DIJOFS_POV0E:
                                        case DIJOFS_POV1E:
                                        case DIJOFS_POV2E:
                                        case DIJOFS_POV3E:
                                            if ((js.rgdwPOV[count2] >= 4500) && (js.rgdwPOV[count2] <= 13500))
                                            {
                                                switch (count2)
                                                {
                                                case 18:
                                                    M1Speed = Controller[Control].Input[count].button;
                                                    break;

                                                case 19:
                                                    M2Speed = Controller[Control].Input[count].button;
                                                    break;

                                                case 0:
                                                case 1:
                                                case 2:
                                                case 3:
                                                    analogKey = true;
                                                /* fall through */
                                                default:
                                                    ControllerInput.Value |= Controller[Control].Input[count].button;
                                                    break;
                                                }
                                            }
                                            break;
                                        case DIJOFS_POV0S:
                                        case DIJOFS_POV1S:
                                        case DIJOFS_POV2S:
                                        case DIJOFS_POV3S:
                                            if ((js.rgdwPOV[count2] >= 13500) && (js.rgdwPOV[count2] <= 22500))
                                            {
                                                switch (count2)
                                                {
                                                case 18:
                                                    M1Speed = Controller[Control].Input[count].button;
                                                    break;

                                                case 19:
                                                    M2Speed = Controller[Control].Input[count].button;
                                                    break;

                                                case 0:
                                                case 1:
                                                case 2:
                                                case 3:
                                                    analogKey = true;
                                                /* fall through */
                                                default:
                                                    ControllerInput.Value |= Controller[Control].Input[count].button;
                                                    break;
                                                }
                                            }
                                            break;
                                        case DIJOFS_POV0W:
                                        case DIJOFS_POV1W:
                                        case DIJOFS_POV2W:
                                        case DIJOFS_POV3W:
                                            if ((js.rgdwPOV[count2] >= 22500) && (js.rgdwPOV[count2] <= 31500))
                                            {
                                                switch (count2)
                                                {
                                                case 18:
                                                    M1Speed = Controller[Control].Input[count].button;
                                                    break;

                                                case 19:
                                                    M2Speed = Controller[Control].Input[count].button;
                                                    break;

                                                case 0:
                                                case 1:
                                                case 2:
                                                case 3:
                                                    analogKey = true;
                                                /* fall through */
                                                default:
                                                    ControllerInput.Value |= Controller[Control].Input[count].button;
                                                    break;
                                                }
                                            }
                                            break;
                                        }
                                    }
                                }

                            default:
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (M2Speed)
        {
            if (ControllerInput.Y_AXIS < 0)
                ControllerInput.Y_AXIS = (char)-M2Speed;
            else if (ControllerInput.Y_AXIS > 0)
                ControllerInput.Y_AXIS = (char)M2Speed;

            if (ControllerInput.X_AXIS < 0)
                ControllerInput.X_AXIS = (char)-M2Speed;
            else if (ControllerInput.X_AXIS > 0)
                ControllerInput.X_AXIS = (char)M2Speed;
        }
        if (M1Speed)
        {
            if (ControllerInput.Y_AXIS < 0)
                ControllerInput.Y_AXIS = (char)-M1Speed;
            else if (ControllerInput.Y_AXIS > 0)
                ControllerInput.Y_AXIS = (char)M1Speed;

            if (ControllerInput.X_AXIS < 0)
                ControllerInput.X_AXIS = (char)-M1Speed;
            else if (ControllerInput.X_AXIS > 0)
                ControllerInput.X_AXIS = (char)M1Speed;
        }
        if (analogKey)
        {
            if (ControllerInput.X_AXIS && ControllerInput.Y_AXIS)
            {
                const static float mult = 1.0f / sqrtf(2.0f);
                float mult2;
                if (Controller[Control].SensMax > 127)
                    mult2 = (float)Controller[Control].SensMax * (1.0f / 127.0f);
                else
                    mult2 = 1.0f;
                if (!relativeXOn)
                    ControllerInput.X_AXIS = (int)(ControllerInput.X_AXIS * mult * mult2 + (ControllerInput.X_AXIS > 0
                        ? 0.5f
                        : -0.5f));
                if (!relativeYOn && relativeXOn != 3)
                    ControllerInput.Y_AXIS = (int)(ControllerInput.Y_AXIS * mult * mult2 + (ControllerInput.Y_AXIS > 0
                        ? 0.5f
                        : -0.5f));

                int newX = (int)((float)ControllerInput.X_AXIS * xScale + (ControllerInput.X_AXIS > 0 ? 0.5f : -0.5f));
                int newY = (int)((float)ControllerInput.Y_AXIS * yScale + (ControllerInput.Y_AXIS > 0 ? 0.5f : -0.5f));
                if (abs(newX) >= abs(newY) && (newX > 127 || newX < -128))
                {
                    newY = newY * (newY > 0 ? 127 : 128) / abs(newX);
                    newX = (newX > 0) ? 127 : -128;
                }
                else if (abs(newX) <= abs(newY) && (newY > 127 || newY < -128))
                {
                    newX = newX * (newX > 0 ? 127 : 128) / abs(newY);
                    newY = (newY > 0) ? 127 : -128;
                }
                if (!newX && ControllerInput.X_AXIS) newX = (ControllerInput.X_AXIS > 0) ? 1 : -1;
                if (!newY && ControllerInput.Y_AXIS) newY = (ControllerInput.Y_AXIS > 0) ? 1 : -1;
                if (!relativeXOn)
                    ControllerInput.X_AXIS = newX;
                if (!relativeYOn && relativeXOn != 3)
                    ControllerInput.Y_AXIS = newY;
            }
            else
            {
                if (ControllerInput.X_AXIS && !relativeXOn)
                {
                    int newX = (int)((float)ControllerInput.X_AXIS * xScale + (
                        ControllerInput.X_AXIS > 0 ? 0.5f : -0.5f));
                    if (!newX && ControllerInput.X_AXIS) newX = (ControllerInput.X_AXIS > 0) ? 1 : -1;
                    ControllerInput.X_AXIS = min(127, max(-128,newX));
                }
                if (ControllerInput.Y_AXIS && !relativeYOn && relativeXOn != 3)
                {
                    int newY = (int)((float)ControllerInput.Y_AXIS * yScale + (
                        ControllerInput.Y_AXIS > 0 ? 0.5f : -0.5f));
                    if (!newY && ControllerInput.Y_AXIS) newY = (ControllerInput.Y_AXIS > 0) ? 1 : -1;
                    ControllerInput.Y_AXIS = min(127, max(-128,newY));
                }
            }
        }
    }

    //here ControllerInput holds true physical controller, which gets modified by tasinput / combo
    DWORD oldOverride = buttonOverride.Value; //so the keys don't stick...
    if (!fakeInput && activeCombo != -1 && (comboTask == C_RUNNING || comboTask == C_LOOP))
    {
            int frame = frameCounter - comboStart;
            if (frame > ACTIVE_COMBO->samples.size() - 1)
            {
                //if frame is out of range
                if (comboTask == C_LOOP)
                {
                    comboStart = frameCounter;
                    frame = 0;
                }
                else
                {
                    if (!overrideOn) //if combo ends and tasinput state wasn't changed, clear
                    {
                        buttonOverride.Value = 0;
                        oldOverride = 0;
                    }
                    set_status("Idle");
                    comboTask = C_IDLE;
                    goto continue_controller;
                }
            }
            char buf[64];
            sprintf(buf, "Playing combo (%d/%d)", frame + 1, ACTIVE_COMBO->samples.size());
            set_status(buf);
            //allows to use joystick with combos
            buttonOverride.Value |= ACTIVE_COMBO->samples[frame].Value;
            // if joystick used, copy the values too (because simply ORing is not enough)
            if (ACTIVE_COMBO->uses_joystick()) 
            {
                overrideX = ControllerInput.X_AXIS = ACTIVE_COMBO->samples[frame].X_AXIS;
                overrideY = ControllerInput.Y_AXIS = ACTIVE_COMBO->samples[frame].Y_AXIS;
            }
    }
    else if (comboTask == C_PAUSE) comboStart++;

continue_controller:
    //Allow unpressing with real controller low iq:
    //1. realChanged has 1 where something changed
    //2. mask out presses, leave releases
    //3. remove the releases from override
    DWORD realChanged = ControllerInput.Value ^ LastPureControllerInput.Value;
    buttonOverride.Value &= ~(realChanged & LastPureControllerInput.Value);

    LastPureControllerInput.Value = ControllerInput.Value;
    ControllerInput.Value |= buttonOverride.Value;
    //if((frameCounter/2)%2 == 0)
    if (frameCounter % 2 == 0) //autofire stuff
        ControllerInput.Value ^= buttonAutofire.Value;
    else
        ControllerInput.Value ^= buttonAutofire2.Value;

    bool prevOverrideAllowed = overrideAllowed;
    overrideAllowed = true;
    if (comboTask != C_PAUSE)
    {
        if (!copyButtons && !fakeInput) SetKeys(ControllerInput); //don't overwrite after switching to read write
        else LastControllerInput = ControllerInput;
        copyButtons = false;
    }
    ControllerInput.X_AXIS = overrideX;
    ControllerInput.Y_AXIS = overrideY;
    //Pass Button Info to Emulator
    Keys->Value = ControllerInput.Value;
    //buttonOverride.Value = oldOverride;
    //copy fetched data to combo too
    if (comboTask == C_RECORD && !fakeInput)
    {
        //extend if full and frame is not 0
        char buf[64];
        sprintf(buf, "Recording combo (%d)", frameCounter - comboStart + 1);
        set_status(buf);

        ACTIVE_COMBO->samples.push_back(*Keys);
    }

    gettingKeys = false;
}

VOID SetXYTextFast(HWND parent, BOOL x, char* str)
{
    // Optimized setdlgitemtext: explanation:
    // GetDlgItem is very slow because of the many controls and this may limit the speed of emulator in some cases(?)
    // Instead of using SetDlgItemText every time and (internally) calling GetDlgItem, we precompute the handles to x and y textboxes the first time and re-use them
    if (!textXHWND || !validatedhTxtbox) textXHWND = GetDlgItem(parent, IDC_EDITX);
    if (!textYHWND || !validatedhTxtbox) textYHWND = GetDlgItem(parent, IDC_EDITY);

    if (x)
        SetWindowText(textXHWND, str);
    else
        SetWindowText(textYHWND, str);
}

//updates buttons
void Status::UpdateVisuals(BUTTONS ControllerInput)
{
#define UPDATECHECK(idc,field) {if(buttonDisplayed.field != ControllerInput.field) CheckDlgButton(statusDlg, idc, ControllerInput.field);}
    UPDATECHECK(IDC_CHECK_A, A_BUTTON);
    UPDATECHECK(IDC_CHECK_B, B_BUTTON);
    UPDATECHECK(IDC_CHECK_START, START_BUTTON);
    UPDATECHECK(IDC_CHECK_L, L_TRIG);
    UPDATECHECK(IDC_CHECK_R, R_TRIG);
    UPDATECHECK(IDC_CHECK_Z, Z_TRIG);
    UPDATECHECK(IDC_CHECK_CUP, U_CBUTTON);
    UPDATECHECK(IDC_CHECK_CLEFT, L_CBUTTON);
    UPDATECHECK(IDC_CHECK_CRIGHT, R_CBUTTON);
    UPDATECHECK(IDC_CHECK_CDOWN, D_CBUTTON);
    UPDATECHECK(IDC_CHECK_DUP, U_DPAD);
    UPDATECHECK(IDC_CHECK_DLEFT, L_DPAD);
    UPDATECHECK(IDC_CHECK_DRIGHT, R_DPAD);
    UPDATECHECK(IDC_CHECK_DDOWN, D_DPAD);
    buttonDisplayed.Value = ControllerInput.Value & 0xFFFF; //fuck off
}

void Status::SetKeys(BUTTONS ControllerInput)
{
    if (copyButtons) //copy m64 data to current input
    {
        buttonOverride.Value = ControllerInput.Value;
        buttonOverride.X_AXIS = buttonOverride.Y_AXIS = 0;
    }
    copyButtons = true;
    bool changed = false;
    if (statusDlg)
    {
        //true if physical controller state is changed (because logical is handled in GetKeys)
        if (buttonDisplayed.Value != ControllerInput.Value)
        {
            UpdateVisuals(ControllerInput);
            buttonDisplayed.Value = ControllerInput.Value;
        }
        if (relativeXOn == 3 && radialRecalc)
        {
            radialDistance = sqrtf(
                (float)((overrideX * overrideX) / (xScale * xScale) + (overrideY * overrideY) / (yScale * yScale)));
            if (radialDistance != 0)
                radialAngle = atan2f((float)-(overrideY / yScale), (float)(overrideX / xScale));
            radialRecalc = false;
        }

        // relative change amount
        int addX = (int)((ControllerInput.X_AXIS * xScale) / 12.0f);
        int addY = (int)((ControllerInput.Y_AXIS * yScale) / 12.0f);

        // calculate fractional (over time) change
        if (relativeControlNow && relativeXOn && ControllerInput.X_AXIS)
        {
            static float incrX = 0.0f;
            incrX += ((ControllerInput.X_AXIS * xScale) / 12.0f) - addX;
            if (incrX > 1.0f)
                addX++, incrX -= 1.0f;
            else if (incrX < -1.0f)
                addX--, incrX += 1.0f;
        }
        if (relativeControlNow && (relativeYOn || relativeXOn == 3) && ControllerInput.Y_AXIS)
        {
            static float incrY = 0.0f;
            incrY += ((ControllerInput.Y_AXIS * yScale) / 12.0f) - addY;
            if (incrY > 1.0f)
                addY++, incrY -= 1.0f;
            else if (incrY < -1.0f)
                addY--, incrY += 1.0f;
        }

        if (relativeXOn && overrideAllowed)
        {
            if (relativeControlNow && ControllerInput.X_AXIS)
            // increment x position by amount relative to attempted new x position
            {
                if (relativeXOn != 3)
                {
                    int nextVal = overrideX + addX;
                    if (nextVal <= 127 && nextVal >= -128)
                    {
                        if (!overrideX || (relativeXOn == 1
                                               ? (((overrideX < 0) == (ControllerInput.X_AXIS < 0)) || !ControllerInput.
                                                   X_AXIS)
                                               : ((overrideX < 0) != (nextVal > 0))))
                            ControllerInput.X_AXIS = nextVal;
                        else
                            ControllerInput.X_AXIS = 0;
                        // lock to 0 once on crossing +/- boundary, or "semi-relative" jump to 0 on direction reversal
                    }
                    else
                    {
                        ControllerInput.X_AXIS = nextVal > 0 ? 127 : -128;
                        if (abs(overrideY + addY) < 129)
                            overrideY = overrideY * 127 / abs(nextVal);
                    }
                }
                else // radial mode, angle change
                {
                    radialAngle += 4 * PI; // keeping it positive
                    float oldAngle = fmodf(radialAngle, 2 * PI);
                    radialAngle = fmodf(
                        radialAngle + (ControllerInput.X_AXIS) / (250.0f + 500.0f / (sqrtf(xScale * yScale) + 0.001f)),
                        2 * PI);

                    // snap where crossing 45 degree boundaries
                    if (radialAngle - oldAngle > PI) radialAngle -= 2 * PI;
                    else if (oldAngle - radialAngle > PI) oldAngle -= 2 * PI;
                    float oldDeg = oldAngle * (180.0f / PI);
                    float newDeg = radialAngle * (180.0f / PI);
                    for (float ang = 0; ang < 360; ang += 45)
                        if (fabsf(oldDeg - ang) > (0.0001f) && (oldDeg < ang) != (newDeg < ang))
                        {
                            radialAngle = ang * (PI / 180.0f);
                            break;
                        }

                    float xAng = xScale * radialDistance * cosf(radialAngle);
                    float yAng = yScale * radialDistance * sinf(radialAngle);
                    int newX = (int)(xAng + (xAng > 0 ? 0.5f : -0.5f));
                    int newY = -(int)(yAng + (yAng > 0 ? 0.5f : -0.5f));
                    if (newX > 127) newX = 127;
                    if (newX < -128) newX = -128;
                    if (newY > 127) newY = 127;
                    if (newY < -128) newY = -128;
                    ControllerInput.X_AXIS = newX;
                    overrideY = newY;
                }
            }
            else
                ControllerInput.X_AXIS = overrideX;
        }
        if ((relativeYOn || (relativeXOn == 3)) && overrideAllowed)
        {
            if (relativeControlNow && (ControllerInput.Y_AXIS || (!relativeYOn && ControllerInput.Y_AXIS !=
                LastControllerInput.Y_AXIS))) // increment y position by amount relative to attempted new y position
            {
                if (relativeXOn != 3) // not a typo (relativeXOn holds radial mode setting)
                {
                    int nextVal = overrideY + addY;
                    if (nextVal <= 127 && nextVal >= -128)
                    {
                        if (!overrideY || (relativeYOn == 1
                                               ? (((overrideY < 0) == (ControllerInput.Y_AXIS < 0)) || !ControllerInput.
                                                   Y_AXIS)
                                               : ((overrideY < 0) != (nextVal > 0))))
                            ControllerInput.Y_AXIS = nextVal;
                        else
                            ControllerInput.Y_AXIS = 0;
                        // lock to 0 once on crossing +/- boundary, or "semi-relative" jump to 0 on direction reversal
                    }
                    else
                    {
                        if (abs(overrideX + addX) < 129)
                            ControllerInput.X_AXIS = ControllerInput.X_AXIS * 127 / abs(nextVal);
                        ControllerInput.Y_AXIS = nextVal > 0 ? 127 : -128;
                    }
                }
                else // radial mode, length change
                {
                    if (!relativeYOn) // relative rotation + instant distance
                    {
                        if (ControllerInput.Y_AXIS < 0)
                            radialDistance = 0;
                        else if (!Controller[Control].SensMin || abs(ControllerInput.Y_AXIS) >= Controller[Control].
                            SensMin)
                        {
                            radialDistance = (float)ControllerInput.Y_AXIS;
                            if (radialDistance == 127) radialDistance = 128;
                        }
                        else
                            radialDistance = (float)Controller[Control].SensMin;
                        LastControllerInput.Y_AXIS = ControllerInput.Y_AXIS;
                    }
                    else if (relativeYOn == 1 && radialDistance && (radialDistance > 0) != (ControllerInput.Y_AXIS > 0))
                        radialDistance = 0; // "semi-relative" distance, jump to 0 on direction reversal
                    else
                        radialDistance += addY;
                    const static float maxDist = sqrtf(128 * 128 + 128 * 128);
                    if (radialDistance > maxDist) radialDistance = maxDist;
                    float xAng = xScale * radialDistance * cosf(radialAngle);
                    float yAng = yScale * radialDistance * sinf(radialAngle);
                    int newX = (int)(xAng + (xAng > 0 ? 0.5f : -0.5f));
                    int newY = -(int)(yAng + (yAng > 0 ? 0.5f : -0.5f));
                    if (newX > 127) newX = 127;
                    if (newX < -128) newX = -128;
                    if (newY > 127) newY = 127;
                    if (newY < -128) newY = -128;
                    ControllerInput.X_AXIS = newX;
                    ControllerInput.Y_AXIS = newY;
                }
            }
            else
                ControllerInput.Y_AXIS = overrideY;
        }
        if (!overrideOn && (LastControllerInput.X_AXIS != ControllerInput.X_AXIS || (AngDisp && LastControllerInput.
            Y_AXIS != ControllerInput.Y_AXIS)))
        {
            char str[256], str2[256];
            GetDlgItemText(statusDlg, IDC_EDITX, str2, 256);
            if (!AngDisp)
                sprintf(str, "%d", ControllerInput.X_AXIS);
            else
            {
                float radialAngle = 4 * PI + atan2f(((float)-ControllerInput.Y_AXIS) / yScale,
                                                    ((float)ControllerInput.X_AXIS) / xScale);
                float angle2 = fmodf(90.0f + radialAngle * (180.0f / PI), 360.0f);
                sprintf(str, "%d", (int)(angle2 + (angle2 > 0 ? 0.5f : -0.5f)));
                skipEditX = true;
                overrideX = (int)ControllerInput.X_AXIS;
            }
            if (strcmp(str, str2))
                //this and the same one for Y editbox is running at a moderate speed
                //SetDlgItemText(statusDlg, IDC_EDITX, str);
                SetXYTextFast(statusDlg, TRUE, str);

            changed = true;
        }
        if (!overrideOn && (LastControllerInput.Y_AXIS != ControllerInput.Y_AXIS || (AngDisp && LastControllerInput.
            X_AXIS != ControllerInput.X_AXIS)))
        {
            char str[256], str2[256];
            GetDlgItemText(statusDlg, IDC_EDITY, str2, 256);
            if (!AngDisp)
                sprintf(str, "%d", -ControllerInput.Y_AXIS);
            else
            {
                float radialDistance = sqrtf(
                    ((float)(ControllerInput.X_AXIS * ControllerInput.X_AXIS) / (xScale * xScale) + (float)(
                        ControllerInput.Y_AXIS * ControllerInput.Y_AXIS) / (yScale * yScale)));
                sprintf(str, "%d", (int)(0.5f + radialDistance));
                skipEditY = true;
                overrideY = (int)ControllerInput.Y_AXIS;
            }
            if (strcmp(str, str2))
                //SetDlgItemText(statusDlg, IDC_EDITY, str);
                SetXYTextFast(statusDlg, FALSE, str);

            changed = true;
        }
    }
    if (relativeYOn || relativeXOn != 3)
        LastControllerInput = ControllerInput;

    if (changed & !overrideOn)
    {
        overrideX = ControllerInput.X_AXIS;
        overrideY = ControllerInput.Y_AXIS;
        RefreshAnalogPicture();
    }
    overrideOn = false; //reset every frame
}


void WINAPI GetNegAxisVal(LONG AxisValue, int Control, LONG count, BUTTONS* ControllerInput, int& M1Speed, int& M2Speed)
{
    switch (count)
    {
    case 0:
        if (AxisValue < (LONG)-Controller[Control].SensMax)
            ControllerInput->Y_AXIS = min(127, Controller[Control].SensMax);
        else
            ControllerInput->Y_AXIS = -AxisValue;
        break;
    case 1:
        if (AxisValue < (LONG)-Controller[Control].SensMax)
            ControllerInput->Y_AXIS = -min(128, Controller[Control].SensMax);
        else
            ControllerInput->Y_AXIS = AxisValue;
        break;
    case 2:
        if (AxisValue < (LONG)-Controller[Control].SensMax)
            ControllerInput->X_AXIS = -min(128, Controller[Control].SensMax);
        else
            ControllerInput->X_AXIS = AxisValue;
        break;
    case 3:
        if (AxisValue < (LONG)-Controller[Control].SensMax)
            ControllerInput->X_AXIS = min(127, Controller[Control].SensMax);
        else
            ControllerInput->X_AXIS = -AxisValue;
        break;

    case 18:
        M1Speed = Controller[Control].Input[count].button;
        break;
    case 19:
        M2Speed = Controller[Control].Input[count].button;
        break;

    default:
        ControllerInput->Value |= Controller[Control].Input[count].button;
        break;
    }
}

void WINAPI GetPosAxisVal(LONG AxisValue, int Control, LONG count, BUTTONS* ControllerInput, int& M1Speed, int& M2Speed)
{
    switch (count)
    {
    case 0:
        if (AxisValue > (LONG)Controller[Control].SensMax)
            ControllerInput->Y_AXIS = min(127, Controller[Control].SensMax);
        else
            ControllerInput->Y_AXIS = AxisValue;
        break;
    case 1:
        if (AxisValue > (LONG)Controller[Control].SensMax)
            ControllerInput->Y_AXIS = -min(128, Controller[Control].SensMax);
        else
            ControllerInput->Y_AXIS = -AxisValue;
        break;
    case 2:
        if (AxisValue > (LONG)Controller[Control].SensMax)
            ControllerInput->X_AXIS = -min(128, Controller[Control].SensMax);
        else
            ControllerInput->X_AXIS = -AxisValue;
        break;
    case 3:
        if (AxisValue > (LONG)Controller[Control].SensMax)
            ControllerInput->X_AXIS = min(127, Controller[Control].SensMax);
        else
            ControllerInput->X_AXIS = AxisValue;
        break;

    case 18:
        M1Speed = Controller[Control].Input[count].button;
        break;
    case 19:
        M2Speed = Controller[Control].Input[count].button;
        break;

    default:
        ControllerInput->Value |= Controller[Control].Input[count].button;
        break;
    }
}


EXPORT void CALL InitiateControllers(HWND hMainWindow, CONTROL Controls[4])
{
    HKEY hKey;
    DWORD dwSize, dwType;

    for (BYTE i = 0; i < NUMBER_OF_CONTROLS; i++)
    {
        ControlDef[i] = &Controls[i];
        ControlDef[i]->Present = FALSE;
        ControlDef[i]->RawData = FALSE;
        ControlDef[i]->Plugin = PLUGIN_NONE;

        Controller[i].NDevices = 0;
        Controller[i].bActive = i == 0 ? TRUE : FALSE;
        Controller[i].SensMax = 128;
        Controller[i].SensMin = 32;
        Controller[i].Input[18].button = 42;
        Controller[i].Input[19].button = 20;
        wsprintf(Controller[i].szName, "Controller %d", i + 1);
    }

    if (g_lpDI == NULL)
    {
        InitializeAndCheckDevices(hMainWindow);
    }

    dwType = REG_BINARY;
    dwSize = sizeof(DEFCONTROLLER);

    if (RegCreateKeyEx(HKEY_CURRENT_USER, SUBKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, 0) != ERROR_SUCCESS)
    {
        MessageBox(NULL, "Could not create Registry Key", "Error", MB_ICONERROR | MB_OK);
    }
    else
    {
        for (BYTE NController = 0; NController < NUMBER_OF_CONTROLS; NController++)
        {
            if (RegQueryValueEx(hKey, Controller[NController].szName, 0, &dwType, (LPBYTE)&Controller[NController],
                                &dwSize) == ERROR_SUCCESS)
            {
                if (Controller[NController].bActive)
                    ControlDef[NController]->Present = TRUE;
                else
                    ControlDef[NController]->Present = FALSE;

                if (Controller[NController].bMemPak)
                    ControlDef[NController]->Plugin = PLUGIN_MEMPAK;
                else
                    ControlDef[NController]->Plugin = PLUGIN_NONE;

                if (dwSize != sizeof(DEFCONTROLLER))
                {
                    dwType = REG_BINARY;
                    dwSize = sizeof(DEFCONTROLLER);
                    ZeroMemory(&Controller[NController], sizeof(DEFCONTROLLER));

                    Controller[NController].NDevices = 0;
                    Controller[NController].bActive = NController == 0 ? TRUE : FALSE;
                    ControlDef[NController]->Present = FALSE;
                    ControlDef[NController]->Plugin = PLUGIN_NONE;
                    Controller[NController].SensMax = 128;
                    Controller[NController].SensMin = 32;
                    Controller[NController].Input[18].button = 42;
                    Controller[NController].Input[19].button = 20;
                    wsprintf(Controller[NController].szName, "Controller %d", NController + 1);

                    RegDeleteValue(hKey, Controller[NController].szName);
                    RegSetValueEx(hKey, Controller[NController].szName, 0, dwType, (LPBYTE)&Controller[NController],
                                  dwSize);
                }
            }
            else
            {
                dwType = REG_BINARY;
                dwSize = sizeof(DEFCONTROLLER);
                RegDeleteValue(hKey, Controller[NController].szName);
                RegSetValueEx(hKey, Controller[NController].szName, 0, dwType, (LPBYTE)&Controller[NController],
                              dwSize);
            }
        }
    }
    RegCloseKey(hKey);
}

void WINAPI InitializeAndCheckDevices(HWND hMainWindow)
{
    HKEY hKey;
    BYTE i;
    DWORD dwSize, dwType;

    //Initialize Direct Input function
    if (FAILED(InitDirectInput(hMainWindow)))
    {
        MessageBox(NULL, "DirectInput Initialization Failed!", "Error",MB_ICONERROR | MB_OK);
        FreeDirectInput();
    }
    else
    {
        dwType = REG_BINARY;
        dwSize = sizeof(Guids);
        //Check Guids for Device Changes
        RegCreateKeyEx(HKEY_CURRENT_USER, SUBKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, 0);
        if (RegQueryValueEx(hKey, TEXT("Guids"), 0, &dwType, (LPBYTE)Guids, &dwSize) != ERROR_SUCCESS)
        {
            for (i = 0; i < MAX_DEVICES; i++)
            {
                if (DInputDev[i].lpDIDevice == NULL)
                    ZeroMemory(&Guids[i], sizeof(GUID));
                else
                    memcpy(&Guids[i], &DInputDev[i].DIDevInst.guidInstance, sizeof(GUID));
            }
            dwType = REG_BINARY;
            RegSetValueEx(hKey, TEXT("Guids"), 0, dwType, (LPBYTE)Guids, dwSize);
        }
        else
        {
            if (CheckForDeviceChange(hKey))
            {
                for (i = 0; i < MAX_DEVICES; i++)
                {
                    if (DInputDev[i].lpDIDevice == NULL)
                        ZeroMemory(&Guids[i], sizeof(GUID));
                    else
                        memcpy(&Guids[i], &DInputDev[i].DIDevInst.guidInstance, sizeof(GUID));
                }
                dwType = REG_BINARY;
                RegSetValueEx(hKey, TEXT("Guids"), 0, dwType, (LPBYTE)Guids, dwSize);
            }
        }
        RegCloseKey(hKey);
    }
}

BOOL WINAPI CheckForDeviceChange(HKEY hKey)
{
    BOOL DeviceChanged;
    DWORD dwSize, dwType;

    dwType = REG_BINARY;
    dwSize = sizeof(DEFCONTROLLER);

    DeviceChanged = FALSE;

    for (BYTE DeviceNumCheck = 0; DeviceNumCheck < MAX_DEVICES; DeviceNumCheck++)
    {
        if (memcmp(&Guids[DeviceNumCheck], &DInputDev[DeviceNumCheck].DIDevInst.guidInstance, sizeof(GUID)) != 0)
        {
            DeviceChanged = TRUE;
            for (BYTE NController = 0; NController < NUMBER_OF_CONTROLS; NController++)
            {
                RegQueryValueEx(hKey, Controller[NController].szName, 0, &dwType, (LPBYTE)&Controller[NController],
                                &dwSize);
                for (BYTE DeviceNum = 0; DeviceNum < Controller[NController].NDevices; DeviceNum++)
                {
                    if (Controller[NController].Devices[DeviceNum] == DeviceNumCheck)
                    {
                        Controller[NController].NDevices = 0;
                        Controller[NController].bActive = FALSE;
                        RegSetValueEx(hKey, Controller[NController].szName, 0, dwType, (LPBYTE)&Controller[NController],
                                      dwSize);
                    }
                }
            }
        }
    }

    return DeviceChanged;
}

EXPORT void CALL ReadController(int Control, BYTE* Command)
{
    // XXX: Increment frame counter here because the plugin specification provides no means of finding out when a frame goes by.
    //      Mupen64 calls ReadController(-1) every input frame, but other emulators might not do that.
    //      (The frame counter is used only for autofire and combo progression.)
    if (Control == -1)
        Status::frameCounter++;

    //for (char i = 0; i < 4; i++)
    //	SendMessage(status[i].statusDlg, WM_SETCURSOR, 0, 0);
}

EXPORT void CALL RomClosed(void)
{
    romIsOpen = false;
    if (fakeStatusThread)
    {
        // really try to nuke it
        printf("Rom opened with fake window");
        DestroyWindow(status[0].statusDlg);
        status[0].statusDlg = NULL;
        status[0].FreeCombos();
        TerminateThread(fakeStatusThread, 0);
        fakeStatusThread = NULL;
        return;
    }
    for (int i = 0; i < NUMBER_OF_CONTROLS; i++)
        status[i].StopThread();
}

void StartFake()
{
    if (fakeStatusThread)
    {
        if (MessageBox(
            0, "You can only have 1 testing TASInput running at a time. Do you want to kill current TASInput instance?",
            "Too many instances", MB_TOPMOST | MB_YESNO) == IDNO)return;
        // kill tasinput
        RomClosed();
        return;
    }

    if (MessageBox(
        0,
        "This is an experimental feature. Are you sure you want to test this plugin? (All combos made with this temporary TASInput will be gone after closing if you don't save)",
        "Test TASInput?", MB_TOPMOST | MB_YESNO) == IDNO) return;
    DWORD dwThreadParam = 0, dwThreadId;
    fakeStatusThread = CreateThread(0, 0, StatusDlgThreadProc, &dwThreadParam, 0, &dwThreadId);
    romIsOpen = true;
}

EXPORT void CALL DllTest(HWND hParent)
{
    StartFake();
}

EXPORT void CALL RomOpen(void)
{
    RomClosed();
    romIsOpen = true;


    HKEY hKey;
    DWORD dwSize, dwType, dwDWSize, dwDWType;

    dwType = REG_BINARY;
    dwSize = sizeof(DEFCONTROLLER);
    dwDWType = REG_DWORD;
    dwDWSize = sizeof(DWORD);

    if (RegOpenKeyEx(HKEY_CURRENT_USER, SUBKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        for (BYTE Control = 0; Control < NUMBER_OF_CONTROLS; Control++)
        {
            RegQueryValueEx(hKey, Controller[Control].szName, 0, &dwType, (LPBYTE)&Controller[Control], &dwSize);
            if (Controller[Control].bActive)
                ControlDef[Control]->Present = TRUE;
            else
                ControlDef[Control]->Present = FALSE;

            char str[256];
            sprintf(str, "Controller%dRelativeX", Control);
            RegQueryValueEx(hKey, str, 0, &dwDWType, (LPBYTE)&status[Control].relativeXOn, &dwDWSize);
            sprintf(str, "Controller%dRelativeY", Control);
            RegQueryValueEx(hKey, str, 0, &dwDWType, (LPBYTE)&status[Control].relativeYOn, &dwDWSize);
            sprintf(str, "Controller%dAngDisp", Control);
            RegQueryValueEx(hKey, str, 0, &dwDWType, (LPBYTE)&status[Control].AngDisp, &dwDWSize);
        }
    }
    RegCloseKey(hKey);

    for (int i = 0; i < NUMBER_OF_CONTROLS; i++)
        if (Controller[i].bActive)
            status[i].StartThread(i);
        else
            status[i].Control = i;
}

EXPORT void CALL WM_KeyDown(WPARAM wParam, LPARAM lParam)
{
}

EXPORT void CALL WM_KeyUp(WPARAM wParam, LPARAM lParam)
{
}

void Status::FreeCombos()
{
    for (auto combo : combos)
    {
        delete combo;
    }
    combos.clear();
}

//sets information about current task
void Status::set_status(std::string str)
{
    HWND hTask = GetDlgItem(statusDlg, IDC_STATUS);
    SendMessage(hTask, WM_SETTEXT, 0, (LPARAM)str.c_str());
}

//Creates new combo, if id!=1 duplicates existing (todo)
int Status::CreateNewCombo(int id)
{
    auto combo = new Combo();
    combos.push_back(combo);
    return ListBox_InsertString(lBox, -1, combo->name.c_str());
}

//shows edit box
void Status::StartEdit(int id)
{
    RECT edit; //, box;
    char txt[MAX_PATH];
    ListBox_GetItemRect(lBox, id, &edit);
    //If editBox isn't child of listbox (which would solve padding issues, but doesn't quite work)
    //GetWindowRect(lBox, &box);
    //POINT pt = { box.left, box.top };
    //ScreenToClient(statusDlg, &pt);
    //edit.top += pt.y;
    //edit.left += pt.x;
    //edit.bottom += pt.y;
    //edit.right += pt.x;
    lock = true;
    HWND editBox = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP, edit.left, edit.top,
                                  edit.right - edit.left, edit.bottom - edit.top + 4, lBox, 0, g_hInstance, 0);
    ListBox_SetCurSel(lBox, -1); //just to be safe, it sometimes can be seen in background

    //FW_BOLD
    //HFONT font = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE, "MS Shell Dlg 2");
    SendMessage(editBox,WM_SETFONT, (WPARAM)SendMessage(lBox, WM_GETFONT, 0, 0), 0);
    SetWindowSubclass(editBox, EditBoxProc, 0, 0); //to add more functionality
    ListBox_GetText(lBox, activeCombo, txt);
    SendMessage(editBox, WM_SETTEXT, 0, (LPARAM)txt);
    PostMessage(statusDlg, WM_NEXTDLGCTL, (WPARAM)editBox, TRUE);
}

void Status::EndEdit(int id, char* name)
{
    if (name != NULL)
    {
        ListBox_DeleteString(lBox, id);
        
        if (name[0] == NULL)
        {
            combos.erase(combos.begin() + id);
        }
        else
        {
            ListBox_InsertString(lBox, id, name);
        }
    }
    set_status("Idle");
}

//saves combos to combos.cmb
void Status::SaveCombos()
{
    save_combos("combos.cmb", combos);
}

//load combos to listBox
void Status::load_combos(const char* path)
{
    combos = find_combos("combos.cmb");

    for (auto combo : combos)
    {
        ListBox_InsertString(lBox, -1, combo->name.c_str());
    }
}

void Status::RefreshAnalogPicture()
{
    RECT rect, rect2;
    GetWindowRect(GetDlgItem(statusDlg,IDC_STICKPIC), &rect);
    GetWindowRect(statusDlg, &rect2);
    rect.left -= rect2.left + 3;
    rect.right -= rect2.left + 3;
    rect.top -= rect2.top + 3;
    rect.bottom -= rect2.top + 3;
    InvalidateRect(statusDlg, &rect, TRUE);
    UpdateWindow(statusDlg);
}

DWORD WINAPI StatusDlgThreadProc(LPVOID lpParameter)
{
    int Control = LOBYTE(*(int*)lpParameter);
    auto extend = HIBYTE(*(int*)lpParameter);
    int DialogID = extend ? IDD_STATUS_COMBOS : IDD_STATUS_NORMAL;

    switch (Control)
    {
    case 0: DialogBox(g_hInstance, MAKEINTRESOURCE(DialogID), NULL, (DLGPROC)StatusDlgProc0);
        break;
    case 1: DialogBox(g_hInstance, MAKEINTRESOURCE(DialogID), NULL, (DLGPROC)StatusDlgProc1);
        break;
    case 2: DialogBox(g_hInstance, MAKEINTRESOURCE(DialogID), NULL, (DLGPROC)StatusDlgProc2);
        break;
    case 3: DialogBox(g_hInstance, MAKEINTRESOURCE(DialogID), NULL, (DLGPROC)StatusDlgProc3);
        break;
    default: DialogBox(g_hInstance, MAKEINTRESOURCE(DialogID), NULL, (DLGPROC)StatusDlgProc0);
    }
    return 0;
}

static bool IsMouseOverControl(HWND hDlg, int dialogItemID)
{
    POINT pt;
    RECT rect;

    GetCursorPos(&pt);
    if (GetWindowRect(GetDlgItem(hDlg, dialogItemID), &rect)) //failed to get the dimensions
        return (pt.x <= rect.right && pt.x >= rect.left && pt.y <= rect.bottom && pt.y >= rect.top);
    return FALSE;
}

void Status::ActivateEmulatorWindow()
{
    if (lock) return;
    if (prevHWnd)
    {
        SetForegroundWindow(prevHWnd);
        return;
    }
    SetFocus(NULL);

    SetActiveWindow(NULL); // activates whatever the previous window was

    HWND hWnd = GetForegroundWindow();
    for (int i = 0; i < NUMBER_OF_CONTROLS; i++)
        if (hWnd == status[i].statusDlg)
            hWnd = NULL;
    if (!hWnd)
    {
        if (prevHWnd)
            BringWindowToTop(prevHWnd); // fix taskbar flash... by just calling another method
        return;
    }

    // try to find out which window belongs to the emulator (the input plugin specifications provide no means of getting this)
    DWORD processID1, processID2;
    GetWindowThreadProcessId(statusDlg, &processID1);
    GetWindowThreadProcessId(hWnd, &processID2);
    if (processID1 == processID2)
        prevHWnd = hWnd;
        // if it's from the same process, remember it (even if we already did this, in case the emulator has multiple windows)
    else if (prevHWnd)
        SetForegroundWindow(prevHWnd);
    // if it's not, the last other window from the same process would be a better match
}

bool Status::IsWindowFromEmulatorProcessActive()
{
    HWND hWnd = GetForegroundWindow();
    for (int i = 0; i < NUMBER_OF_CONTROLS; i++)
        if (hWnd == status[i].statusDlg)
            hWnd = NULL;
    if (!hWnd) return true;
    DWORD processID1, processID2;
    GetWindowThreadProcessId(statusDlg, &processID1);
    GetWindowThreadProcessId(hWnd, &processID2);
    return (processID1 == processID2);
}

bool Status::IsAnyStatusDialogActive()
{
    HWND hWnd = GetForegroundWindow();
    if (!hWnd) return false;
    for (int i = 0; i < NUMBER_OF_CONTROLS; i++)
        if (hWnd == status[i].statusDlg)
            return true;
    return false;
}

//called after right-click menu closes, used to apply some changes
void RefreshChanges(HWND hwnd)
{
    if (menuConfig.floatFromParent)
    {
        SetWindowLongA(hwnd, GWL_STYLE,DS_SETFONT | DS_MODALFRAME | DS_3DLOOK | DS_FIXEDSYS | WS_POPUP | WS_VISIBLE);
        SetWindowLongA(hwnd, GWL_EXSTYLE, WS_EX_STATICEDGE);
    }
    else
    {
        SetWindowLongA(hwnd, GWL_STYLE,
                       DS_SYSMODAL | DS_SETFONT | DS_MODALFRAME | DS_3DLOOK | DS_FIXEDSYS | WS_POPUP | WS_VISIBLE);
        SetWindowLongA(hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_STATICEDGE);
    }

    if (menuConfig.onTop)
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    else
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

bool ShowContextMenu(HWND hwnd, HWND hitwnd, int x, int y)
{
    if (hitwnd != hwnd || IsMouseOverControl(hwnd, IDC_STICKPIC) || (GetKeyState(MOUSE_LBUTTONREDEFINITION) & 0x8000))
        return TRUE;
    RefreshChanges(hwnd);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW); //disable topmost for a second
    hMenu = CreatePopupMenu();
    AppendMenu(hMenu, menuConfig.onTop ? MF_CHECKED : 0, OnTop, "Stay on Top");
    AppendMenu(hMenu, menuConfig.floatFromParent ? MF_CHECKED : 0, Float, "Show in Taskbar");
    AppendMenu(hMenu, menuConfig.movable ? MF_CHECKED : 0, Movable, "Movable");
    lock = true;
    int res = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, x, y, hwnd, 0);
    lock = false;
    printf("trackmenu result %d\n", res);
    switch (res)
    {
    case OnTop:
        menuConfig.onTop ^= 1;
        break;
    case Float:
        menuConfig.floatFromParent ^= 1;
        break;
    case Movable:
        menuConfig.movable ^= 1;
        break;
    }
    RefreshChanges(hwnd);
    DestroyMenu(hMenu);
    return TRUE;
}

//LRESULT CALLBACK StatusDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
//{
//	// awkward way of getting our parameter, as this is a callback function...
//	int i;
//	if(msg == WM_INITDIALOG)
//	{
//		i = lParam;
//		status[i].statusDlg = hDlg;
//	}
//	else
//	{
//		for(i=0; i<NUMBER_OF_CONTROLS; i++)
//			if(hDlg == status[i].statusDlg)
//				break;
//		if(i == NUMBER_OF_CONTROLS)
//		    return DefWindowProc (hDlg, msg, wParam, lParam);
//	}
//
//	return status[i].StatusDlgMethod(msg, wParam, lParam);
//}

#define MAKE_DLG_PROC(i) \
LRESULT CALLBACK StatusDlgProc##i (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) \
{ \
	status[i].statusDlg = hDlg; \
	return status[i].StatusDlgMethod(msg, wParam, lParam); \
}
MAKE_DLG_PROC(0)
MAKE_DLG_PROC(1)
MAKE_DLG_PROC(2)
MAKE_DLG_PROC(3)


LRESULT Status::StatusDlgMethod(UINT msg, WPARAM wParam, LPARAM lParam)
{
    static bool last_rmb_down = false;
    bool rmb_just_down = GetAsyncKeyState(MOUSE_RBUTTONREDEFINITION) & 0x8000 && !last_rmb_down;

    
    if (initialized || msg == WM_INITDIALOG /*|| msg == WM_DESTROY || msg == WM_NCDESTROY*/)
        switch (msg)
        {
        case WM_CONTEXTMENU:
            if (!ShowContextMenu(statusDlg, (HWND)wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        //DefWindowProc()
            break;

        case WM_ERASEBKGND:
            if (once)
            {
                once = false;
                break;
            }
            return TRUE;
        case WM_INITDIALOG:
            {
                // reset some dialog state
                lastXDrag = 0;
                lastYDrag = 0;
                dragging = false, draggingStick = false, draggingPermaStick = false;
                dragXStart = 0, dragYStart = 0;
                lastXDrag = 0, lastYDrag = 0;
                xScale = 1.0f;
                yScale = 1.0f;
                deactivateAfterClick = false;
                int initialStickX = overrideX;
                int initialStickY = overrideY;

                // set title
                char str[256];
                sprintf(str, "Analog Stick - Controller %d", Control + 1);
                SetWindowText(GetDlgItem(statusDlg,IDC_ANALOGSTICKLABEL), str);
                sprintf(str, "Buttons - Controller %d", Control + 1);
                SetWindowText(GetDlgItem(statusDlg,IDC_BUTTONSLABEL), str);
                sprintf(str, "Combos - Controller %d", Control + 1); // XXX
                SetWindowText(GetDlgItem(statusDlg,IDC_COMBOLABEL), str);
                if (AngDisp)
                {
                    CheckDlgButton(statusDlg, IDC_CHECK_ANGDISP, TRUE);
                    SetDlgItemText(statusDlg, IDC_STATICX, "a");
                    SetDlgItemText(statusDlg, IDC_STATICY, "D");
                }
                else
                {
                    CheckDlgButton(statusDlg, IDC_CHECK_ANGDISP, FALSE);
                    SetDlgItemText(statusDlg, IDC_STATICX, "X");
                    SetDlgItemText(statusDlg, IDC_STATICY, "Y");
                }

                // calculate rects
                RECT dlgRect, picRect;
                GetWindowRect(statusDlg, &dlgRect);
                GetWindowRect(GetDlgItem(statusDlg,IDC_STICKPIC), &picRect);
                picRect.left -= dlgRect.left;
                picRect.right -= dlgRect.left;
                picRect.top -= dlgRect.top;
                picRect.bottom -= dlgRect.top;

                // move window into position
                if (!positioned)
                {
                    xPosition = Control * (dlgRect.right - dlgRect.left);
                    yPosition = 0;
                    positioned = true;
                }
                SetWindowPos(statusDlg, 0, xPosition, yPosition, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_SHOWWINDOW);

                // set ranges
                if (!AngDisp)
                {
                    SendDlgItemMessage(statusDlg, IDC_SPINX, UDM_SETRANGE, 0, (LPARAM)MAKELONG(127, -128));
                    SendDlgItemMessage(statusDlg, IDC_SPINY, UDM_SETRANGE, 0, (LPARAM)MAKELONG(-128, 127));
                }
                else
                {
                    SendDlgItemMessage(statusDlg, IDC_SPINX, UDM_SETRANGE, 0, (LPARAM)MAKELONG(720, -720));
                    SendDlgItemMessage(statusDlg, IDC_SPINY, UDM_SETRANGE, 0, (LPARAM)MAKELONG(180, -180));
                }
                SendDlgItemMessage(statusDlg, IDC_SLIDERX, TBM_SETRANGE, TRUE, (LPARAM)MAKELONG(10, 2010));
                SendDlgItemMessage(statusDlg, IDC_SLIDERX, TBM_SETPOS, TRUE, 1000);
                SendDlgItemMessage(statusDlg, IDC_SLIDERY, TBM_SETRANGE, TRUE, (LPARAM)MAKELONG(10, 2010));
                SendDlgItemMessage(statusDlg, IDC_SLIDERY, TBM_SETPOS, TRUE, 1000);

                // set checkbox initial states

                CheckDlgButton(statusDlg, IDC_XABS, relativeXOn == 0 ? TRUE : FALSE);
                CheckDlgButton(statusDlg, IDC_YABS, relativeYOn == 0 ? TRUE : FALSE);
                CheckDlgButton(statusDlg, IDC_XSEM, relativeXOn == 1 ? TRUE : FALSE);
                CheckDlgButton(statusDlg, IDC_YSEM, relativeYOn == 1 ? TRUE : FALSE);
                CheckDlgButton(statusDlg, IDC_XREL, relativeXOn == 2 ? TRUE : FALSE);
                CheckDlgButton(statusDlg, IDC_YREL, relativeYOn == 2 ? TRUE : FALSE);
                CheckDlgButton(statusDlg, IDC_XRAD, relativeXOn == 3 ? TRUE : FALSE);
                CheckDlgButton(statusDlg, IDC_CHECK_A, buttonDisplayed.A_BUTTON);
                CheckDlgButton(statusDlg, IDC_CHECK_B, buttonDisplayed.B_BUTTON);
                CheckDlgButton(statusDlg, IDC_CHECK_START, buttonDisplayed.START_BUTTON);
                CheckDlgButton(statusDlg, IDC_CHECK_L, buttonDisplayed.L_TRIG);
                CheckDlgButton(statusDlg, IDC_CHECK_R, buttonDisplayed.R_TRIG);
                CheckDlgButton(statusDlg, IDC_CHECK_Z, buttonDisplayed.Z_TRIG);
                CheckDlgButton(statusDlg, IDC_CHECK_CUP, buttonDisplayed.U_CBUTTON);
                CheckDlgButton(statusDlg, IDC_CHECK_CLEFT, buttonDisplayed.L_CBUTTON);
                CheckDlgButton(statusDlg, IDC_CHECK_CRIGHT, buttonDisplayed.R_CBUTTON);
                CheckDlgButton(statusDlg, IDC_CHECK_CDOWN, buttonDisplayed.D_CBUTTON);
                CheckDlgButton(statusDlg, IDC_CHECK_DUP, buttonDisplayed.U_DPAD);
                CheckDlgButton(statusDlg, IDC_CHECK_DLEFT, buttonDisplayed.L_DPAD);
                CheckDlgButton(statusDlg, IDC_CHECK_DRIGHT, buttonDisplayed.R_DPAD);
                CheckDlgButton(statusDlg, IDC_CHECK_DDOWN, buttonDisplayed.D_DPAD);

                // begin accepting other messages
                initialized = true;

                // initial x/y text field values
                sprintf(str, "%d", initialStickX);
                SetDlgItemText(statusDlg, IDC_EDITX, str); // no need for "fast" version here this only happens one time
                sprintf(str, "%d", -initialStickY);
                SetDlgItemText(statusDlg, IDC_EDITY, str);

                //Load combos
                //I realised there's HasPanel() too, but doesn't make much difference
                lBox = GetDlgItem(statusDlg, IDC_MACROLIST);
                if (lBox)
                {
                    FreeCombos();
                    load_combos("combos.cmb");
                }
                // switch to emulator
                if (IsWindowFromEmulatorProcessActive())
                    ActivateEmulatorWindow();
            }
            break;

        case WM_ACTIVATE:
        case WM_SETFOCUS:
            {
                if (IsWindowFromEmulatorProcessActive())
                    ActivateEmulatorWindow();
            }
            break;
        case SC_MINIMIZE:
            DestroyMenu(hMenu); // nuke context menu when minimized...
            break;
        case WM_NCDESTROY:
        case WM_DESTROY:
            {
                initialized = false;
                //			buttonOverride.Value = 0;
                //			buttonAutofire.Value = 0;
                //			overrideOn = false;
                KillTimer(statusDlg, IDT_TIMER3);
                // XXX: what about the other windows' timers? We don't necessarily want to kill them. But this... doesn't work anyway?
                statusDlg = NULL;
            }
            break;

        // too bad we don't get useful events like WM_MOUSEMOVE or WM_LBUTTONDOWN...
        case WM_SETCURSOR:
            {
                if (rmb_just_down && IsMouseOverControl(statusDlg, IDC_SLIDERX))
                {
                    SendDlgItemMessage(statusDlg, IDC_SLIDERX, TBM_SETPOS, TRUE, (LPARAM)(LONG)(1000));
                    deactivateAfterClick = true;
                }

                if (rmb_just_down && IsMouseOverControl(statusDlg, IDC_SLIDERY))
                {
                    SendDlgItemMessage(statusDlg, IDC_SLIDERY, TBM_SETPOS, TRUE, (LPARAM)(LONG)(1000));
                    deactivateAfterClick = true;
                }

                // update autofire state
                if (rmb_just_down && IsMouseOverControl(statusDlg, IDC_BUTTONSLABEL))
                {
                    overrideOn = true; 

                    UPDATEAUTO(IDC_CHECK_A, A_BUTTON);
                    UPDATEAUTO(IDC_CHECK_B, B_BUTTON);
                    UPDATEAUTO(IDC_CHECK_START, START_BUTTON);
                    UPDATEAUTO(IDC_CHECK_L, L_TRIG);
                    UPDATEAUTO(IDC_CHECK_R, R_TRIG);
                    UPDATEAUTO(IDC_CHECK_Z, Z_TRIG);
                    UPDATEAUTO(IDC_CHECK_CUP, U_CBUTTON);
                    UPDATEAUTO(IDC_CHECK_CLEFT, L_CBUTTON);
                    UPDATEAUTO(IDC_CHECK_CRIGHT, R_CBUTTON);
                    UPDATEAUTO(IDC_CHECK_CDOWN, D_CBUTTON);
                    UPDATEAUTO(IDC_CHECK_DUP, U_DPAD);
                    UPDATEAUTO(IDC_CHECK_DLEFT, L_DPAD);
                    UPDATEAUTO(IDC_CHECK_DRIGHT, R_DPAD);
                    UPDATEAUTO(IDC_CHECK_DDOWN, D_DPAD);

                    ActivateEmulatorWindow();
                }
                last_rmb_down = GetAsyncKeyState(MOUSE_RBUTTONREDEFINITION) & 0x8000;
            }
        break;
        case WM_MOUSEMOVE:
        case WM_NCHITTEST:
        case WM_TIMER:
            skipEditX = false;
            skipEditY = false;
            if (dragging)
            {
                POINT pt;
                GetCursorPos(&pt);
                int newDragX = pt.x;
                int newDragY = pt.y;
                if (lastXDrag != newDragX - dragXStart || lastYDrag != newDragY - dragYStart)
                {
                    lastXDrag = newDragX - dragXStart;
                    lastYDrag = newDragY - dragYStart;
                    // do not
                    SetWindowPos(statusDlg, 0, lastXDrag, lastYDrag, 0, 0,/*SWP_NOZORDER|*/SWP_NOSIZE | SWP_SHOWWINDOW);
                }
            }
            else if (draggingStick)
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(GetDlgItem(statusDlg, IDC_STICKPIC), &pt);

                RECT picRect;
                GetWindowRect(GetDlgItem(statusDlg, IDC_STICKPIC), &picRect);

                overrideX = (pt.x * 256 / (signed)(picRect.right - picRect.left) - 128 + 1);
                overrideY = -(pt.y * 256 / (signed)(picRect.bottom - picRect.top) - 128 + 1);

                // normalize out-of-bounds clicks
                if (overrideX > 127 || overrideY > 127 || overrideX < -128 || overrideY < -129)
                {
                    int absX = abs(overrideX);
                    int absY = abs(overrideY);
                    int div = (absX > absY) ? absX : absY;
                    overrideX = overrideX * (overrideX > 0 ? 127 : 128) / div;
                    overrideY = overrideY * (overrideY > 0 ? 127 : 128) / div;
                }
                if (overrideX < 7 && overrideX > -7) // snap near-zero clicks to zero
                    overrideX = 0;
                if (overrideY < 7 && overrideY > -7)
                    overrideY = 0;

                char str[256];
                if (!AngDisp)
                    sprintf(str, "%d", overrideX);
                else
                {
                    float radialAngle = 4 * PI + atan2f((float)-overrideY, (float)overrideX);
                    float angle2 = fmodf(90.0f + radialAngle * (180.0f / PI), 360.0f);
                    sprintf(str, "%d", (int)(angle2 + (angle2 > 0 ? 0.5f : -0.5f)));
                    skipEditX = true;
                }
                SetXYTextFast(statusDlg, true, str);
                if (!AngDisp)
                    sprintf(str, "%d", -overrideY);
                else
                {
                    float radialDistance = sqrtf((float)(overrideX * overrideX + overrideY * overrideY));
                    sprintf(str, "%d", (int)(0.5f + radialDistance));
                    skipEditY = true;
                }
                SetXYTextFast(statusDlg, false, str);
                radialRecalc = true;
                overrideOn = true; //joystick dragged with mouse
                RefreshAnalogPicture();
            }
            else if (IsWindowFromEmulatorProcessActive() && !lock)
            {
                if (!IsAnyStatusDialogActive())
                {
                    if (gettingKeys)
                        Sleep(0);
                    //ActivateEmulatorWindow();
                    if (!gettingKeys && !(comboTask & (C_RUNNING | C_LOOP)) && !copyButtons)
                    {
                        BUTTONS Keys;
                        relativeControlNow = (msg == WM_TIMER);
                        fakeInput = true;
                        GetKeys(&Keys); //used in radial mode I think
                        fakeInput = false;
                        relativeControlNow = false;
                        //ActivateEmulatorWindow();
                    }
                }
                else
                {
                    if (((GetKeyState(VK_ESCAPE) & 0x8000) || (GetKeyState(VK_RETURN) & 0x8000) || (deactivateAfterClick
                        && !(GetAsyncKeyState(MOUSE_LBUTTONREDEFINITION) & 0x8000))))
                    {
                        ActivateEmulatorWindow();
                        deactivateAfterClick = false;
                    }
                }
                SetWindowPos(statusDlg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
            break;

        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdcmain = BeginPaint(statusDlg, &ps);
                HDC hdc = CreateCompatibleDC(hdcmain); //buffer 
                RECT rect, rect2;
                GetWindowRect(GetDlgItem(statusDlg,IDC_STICKPIC), &rect);
                GetWindowRect(statusDlg, &rect2);
                //move to relative of whole window, adjusted because ellipse sucks and because bottom right is exclusive
                rect.left -= rect2.left + 2;
                rect.right -= rect2.left + 4;
                rect.top -= rect2.top + 2;
                rect.bottom -= rect2.top + 4;

                int w = rect.right;
                int h = rect.bottom;
                HBITMAP hBmp = CreateCompatibleBitmap(hdcmain, w, h);
                //!!! it has to be compatible with  main dc, not buffer!
                SelectObject(hdc, hBmp); //use the bmp to draw

                FillRect(hdc, &rect, GetSysColorBrush(COLOR_BTNFACE)); //I clear background on my own
                Ellipse(hdc, rect.left, rect.top, rect.right, rect.bottom);

                HPEN hpenOld, hpenBlue, hpenRed;
                hpenBlue = CreatePen(PS_SOLID, 3, RGB(0, 0, 255)); // these need to be re-created every time...
                hpenRed = CreatePen(PS_SOLID, 7, RGB(255, 0, 0));
                hpenOld = (HPEN)SelectObject(hdc, hpenBlue);

                MoveToEx(hdc, (rect.left + rect.right) >> 1, (rect.top + rect.bottom) >> 1, NULL);
                int x = rect.left + (overrideX + 128) * (rect.right - rect.left) / 256;
                int y = rect.top + (-overrideY + 128) * (rect.bottom - rect.top) / 256;
                LineTo(hdc, x, y);

                SelectObject(hdc, hpenOld);
                DeleteObject(hpenBlue);

                MoveToEx(hdc, rect.left, (rect.top + rect.bottom) >> 1, NULL);
                LineTo(hdc, rect.right, (rect.top + rect.bottom) >> 1);
                MoveToEx(hdc, (rect.left + rect.right) >> 1, rect.top, NULL);
                LineTo(hdc, (rect.left + rect.right) >> 1, rect.bottom);

                hpenOld = (HPEN)SelectObject(hdc, hpenRed);
                MoveToEx(hdc, x, y, NULL);
                LineTo(hdc, x, y);
                SelectObject(hdc, hpenOld);
                DeleteObject(hpenRed);
                BitBlt(hdcmain, rect.left, rect.top, w, h, hdc, rect.left, rect.top, SRCCOPY);
                EndPaint(statusDlg, &ps);
                //free everything to avoid memory leak
                DeleteDC(hdc);
                DeleteDC(hdcmain);
                DeleteObject(hBmp);
                DeleteObject(hpenOld);
            }

            break;
        case WM_NOTIFY:
            {
                switch (LOWORD(wParam))
                {
                case IDC_SPINX:
                case IDC_SPINY:
                    {
                        if (GetAsyncKeyState(MOUSE_LBUTTONREDEFINITION) & 0x8000)
                            deactivateAfterClick = true;
                        overrideOn = true;
                    }
                    break;

                case IDC_SLIDERX:
                    {
                        int pos = SendDlgItemMessage(statusDlg, IDC_SLIDERX, TBM_GETPOS, 0, 0);
                        xScale = pos / 1000.0f;
                    }
                    break;

                case IDC_SLIDERY:
                    {
                        int pos = SendDlgItemMessage(statusDlg, IDC_SLIDERY, TBM_GETPOS, 0, 0);
                        yScale = pos / 1000.0f;
                    }
                    break;
                }
            }
            break;

        case WM_LBUTTONUP:
            if (dragging)
            {
                printf("drag end\n");
                dragging = false;
                ReleaseCapture();
                ActivateEmulatorWindow();
            }
            if (draggingStick)
            {
                draggingStick = false;
                ReleaseCapture();
                if (IsWindowFromEmulatorProcessActive())
                    ActivateEmulatorWindow();
            }
            if (draggingPermaStick)
            {
                draggingPermaStick = false;
                KillTimer(statusDlg, IDT_TIMER3);
            }
            break;

        case WM_LBUTTONDOWN:
            //this message is only sent when clicking on non-controls, which is perfect for dragging right
            printf("ld\n");
            if (!IsMouseOverControl(statusDlg,IDC_STICKPIC))
            {
                if (menuConfig.movable)
                {
                    POINT pt;
                    printf("drag start\n");
                    dragging = true;
                    GetCursorPos(&pt);
                    dragXStart = pt.x;
                    dragYStart = pt.y;

                    RECT rect;
                    GetWindowRect(statusDlg, &rect);
                    dragXStart -= rect.left;
                    dragYStart -= rect.top;
                }
            }
            else
            {
                draggingStick = true;
                SendMessage(statusDlg, WM_MOUSEMOVE, 0, 0); //updates stick
                ActivateEmulatorWindow();
            }
            SetCapture(statusDlg); //let mouse escape window

            break;
        case WM_RBUTTONDOWN:
            printf("rd\n");
        //flip permadrag state and update dragging stick accordingly
            if (IsMouseOverControl(statusDlg, IDC_STICKPIC))
            {
                draggingPermaStick = !draggingPermaStick;
                if (draggingStick = draggingPermaStick)
                {
                    ActivateEmulatorWindow();
                    SetTimer(statusDlg, IDT_TIMER3, 50, (TIMERPROC)NULL);
                }
                else KillTimer(statusDlg, IDT_TIMER3);
            }
            break;
        case EDIT_END:
            EndEdit(activeCombo, (char*)lParam);
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_EDITX:
                if (!skipEditX)
                {
                    bool changed = false;
                    char str[256];
                    int newOverrideX = overrideX;
                    GetDlgItemText(statusDlg, IDC_EDITX, str, 256);
                    if (!AngDisp)
                    {
                        sscanf(str, "%d", &newOverrideX);
                        if (newOverrideX > 127 || newOverrideX < -128)
                        {
                            if (newOverrideX > 127) newOverrideX = 127;
                            if (newOverrideX < -128) newOverrideX = -128;
                            sprintf(str, "%d", newOverrideX);
                            SetXYTextFast(statusDlg, true, str);
                        }
                    }
                    else
                    {
                        int newAng;
                        if (sscanf(str, "%d", &newAng))
                        {
                            if (newAng >= 360)
                            {
                                sprintf(str, "%d", newAng - 360);
                                SetXYTextFast(statusDlg, true, str);
                            }
                            else if (newAng < 0)
                            {
                                sprintf(str, "%d", newAng + 360);
                                SetXYTextFast(statusDlg, true, str);
                            }
                            float newAngF = (newAng - 90) * (PI / 180.0f);
                            newOverrideX = (int)(xScale * radialDistance * cosf((float)newAngF));
                            overrideY = -(int)(yScale * radialDistance * sinf((float)newAngF));
                            if (newOverrideX > 127) newOverrideX = 127;
                            if (newOverrideX < -128) newOverrideX = -128;
                            if (overrideY > 127) overrideY = 127;
                            if (overrideY < -128) overrideY = -128;
                        }
                    }
                    if (overrideX != newOverrideX)
                    {
                        changed = true;
                        overrideX = newOverrideX;
                    }
                    if (changed)
                    {
                        //overrideOn = true; //no, because it's also called when editbox is being edited by m64 play, which doesn't mean joystick was draggeds
                        RefreshAnalogPicture();
                    }
                }
                break;

            case IDC_EDITY:
                if (!skipEditY)
                {
                    bool changed = false;
                    char str[256];
                    int newOverrideY = overrideY;
                    GetDlgItemText(statusDlg, IDC_EDITY, str, 256);
                    if (!AngDisp)
                    {
                        sscanf(str, "%d", &newOverrideY);
                        newOverrideY = -newOverrideY;
                        if (newOverrideY > 127 || newOverrideY < -128)
                        {
                            if (newOverrideY > 127) newOverrideY = 127;
                            if (newOverrideY < -128) newOverrideY = -128;
                            sprintf(str, "%d", -newOverrideY);
                            SetXYTextFast(statusDlg, false, str);
                        }
                    }
                    else
                    {
                        int newDist = (int)radialDistance;
                        sscanf(str, "%d", &newDist);
                        overrideX = (int)(xScale * (float)newDist * cosf(radialAngle));
                        newOverrideY = -(int)(yScale * (float)newDist * sinf(radialAngle));
                        if (newOverrideY > 127) newOverrideY = 127;
                        if (newOverrideY < -128) newOverrideY = -128;
                        if (overrideX > 127) overrideX = 127;
                        if (overrideX < -128) overrideX = -128;
                    }
                    if (overrideY != newOverrideY)
                    {
                        changed = true;
                        overrideY = newOverrideY;
                    }
                    if (changed)
                    {
                        //overrideOn = true;  //same here
                        RefreshAnalogPicture();
                    }
                }
                break;

            case IDC_XREL:
            case IDC_XSEM:
            case IDC_XABS:
            case IDC_XRAD:
            case IDC_YREL:
            case IDC_YSEM:
            case IDC_YABS:
                {
                    if (IsDlgButtonChecked(statusDlg, IDC_XABS)) relativeXOn = 0;
                    if (IsDlgButtonChecked(statusDlg, IDC_YABS)) relativeYOn = 0;
                    if (IsDlgButtonChecked(statusDlg, IDC_XSEM)) relativeXOn = 1;
                    if (IsDlgButtonChecked(statusDlg, IDC_YSEM)) relativeYOn = 1;
                    if (IsDlgButtonChecked(statusDlg, IDC_XREL)) relativeXOn = 2;
                    if (IsDlgButtonChecked(statusDlg, IDC_YREL)) relativeYOn = 2;
                    if (IsDlgButtonChecked(statusDlg, IDC_XRAD))
                    {
                        relativeXOn = 3;
                        SetTimer(statusDlg, IDT_TIMER3, 50, (TIMERPROC)NULL);
                    }
                    if (relativeXOn != 3) KillTimer(statusDlg, IDT_TIMER3);
                    // Workaround for wine: this will start or stop the application-breaking timer depending on radial mode enabled or not...
                    // no one uses radial anyway so we're good

                    radialRecalc = true;

                    HKEY hKey;
                    DWORD dwDWType = REG_DWORD;
                    DWORD dwDWSize = sizeof(DWORD);
                    if (RegOpenKeyEx(HKEY_CURRENT_USER, SUBKEY, 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS)
                    {
                        char str[256];
                        sprintf(str, "Controller%dRelativeX", Control);
                        RegSetValueEx(hKey, str, 0, dwDWType, (LPBYTE)&relativeXOn, dwDWSize);
                        sprintf(str, "Controller%dRelativeY", Control);
                        RegSetValueEx(hKey, str, 0, dwDWType, (LPBYTE)&relativeYOn, dwDWSize);
                    }
                    RegCloseKey(hKey);

                    ActivateEmulatorWindow();
                }
                break;

            case IDC_CHECK_ANGDISP:
                {
                    if (IsDlgButtonChecked(statusDlg, IDC_CHECK_ANGDISP))
                    {
                        AngDisp = true;
                        SetDlgItemText(statusDlg, IDC_STATICX, "a");
                        SetDlgItemText(statusDlg, IDC_STATICY, "D");
                        SendDlgItemMessage(statusDlg, IDC_SPINX, UDM_SETRANGE, 0, (LPARAM)MAKELONG(720, -720));
                        SendDlgItemMessage(statusDlg, IDC_SPINY, UDM_SETRANGE, 0, (LPARAM)MAKELONG(180, -180));
                    }
                    else
                    {
                        AngDisp = false;
                        SetDlgItemText(statusDlg, IDC_STATICX, "X");
                        SetDlgItemText(statusDlg, IDC_STATICY, "Y");
                        SendDlgItemMessage(statusDlg, IDC_SPINX, UDM_SETRANGE, 0, (LPARAM)MAKELONG(128, -127));
                        SendDlgItemMessage(statusDlg, IDC_SPINY, UDM_SETRANGE, 0, (LPARAM)MAKELONG(-127, 128));
                    }

                    HKEY hKey;
                    DWORD dwDWType = REG_DWORD;
                    DWORD dwDWSize = sizeof(DWORD);
                    if (RegOpenKeyEx(HKEY_CURRENT_USER, SUBKEY, 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS)
                    {
                        char str[256];
                        sprintf(str, "Controller%dAngDisp", Control);
                        RegSetValueEx(hKey, str, 0, dwDWType, (LPBYTE)&AngDisp, dwDWSize);
                    }
                    RegCloseKey(hKey);

                    ActivateEmulatorWindow();
                }
                break;
            //on checkbox click set buttonOverride and buttonDisplayed field and reset autofire
            case IDC_CHECK_A: buttonOverride.A_BUTTON = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.A_BUTTON = buttonAutofire2.A_BUTTON = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_B: buttonOverride.B_BUTTON = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.B_BUTTON = buttonAutofire2.B_BUTTON = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_START: buttonOverride.START_BUTTON = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.START_BUTTON = buttonAutofire2.START_BUTTON = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_Z: buttonOverride.Z_TRIG = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.Z_TRIG = buttonAutofire2.Z_TRIG = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_L: buttonOverride.L_TRIG = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.L_TRIG = buttonAutofire2.L_TRIG = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_R: buttonOverride.R_TRIG = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.R_TRIG = buttonAutofire2.R_TRIG = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_CLEFT: buttonOverride.L_CBUTTON = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.L_CBUTTON = buttonAutofire2.L_CBUTTON = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_CUP: buttonOverride.U_CBUTTON = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.U_CBUTTON = buttonAutofire2.U_CBUTTON = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_CRIGHT: buttonOverride.R_CBUTTON = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.R_CBUTTON = buttonAutofire2.R_CBUTTON = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_CDOWN: buttonOverride.D_CBUTTON = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.D_CBUTTON = buttonAutofire2.D_CBUTTON = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_DLEFT: buttonOverride.L_DPAD = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.L_DPAD = buttonAutofire2.L_DPAD = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_DUP: buttonOverride.U_DPAD = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.U_DPAD = buttonAutofire2.U_DPAD = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_DRIGHT: buttonOverride.R_DPAD = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.R_DPAD = buttonAutofire2.R_DPAD = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CHECK_DDOWN: buttonOverride.D_DPAD = IsDlgButtonChecked(statusDlg, LOWORD(wParam)) ? 1 : 0;
                buttonAutofire.D_DPAD = buttonAutofire2.D_DPAD = 0;
                ActivateEmulatorWindow();
                break;
            case IDC_CLEARINPUT:
                if (GetKeyState(VK_MENU) & 0x8000)
                {
                    overrideAllowed = true;
                    overrideOn = true;
                    overrideX = 0;
                    overrideY = 0;
                    SetDlgItemText(statusDlg, IDC_EDITY, "0");
                    SetDlgItemText(statusDlg, IDC_EDITX, "0");
                    RefreshAnalogPicture();
                    ActivateEmulatorWindow();
                }
                else {
                    buttonOverride.Value = buttonAutofire.Value = buttonAutofire2.Value = 0;
                    GetKeys(0);
                    ActivateEmulatorWindow();
                }
                break;
            case IDC_MOREBUTTON4:
            case IDC_MOREBUTTON5:
                {
                    switch (LOWORD(wParam)) // Extend
                    {
                    case IDC_MOREBUTTON4: Extend = 1;
                        break;
                    case IDC_MOREBUTTON5: Extend = 0;
                        break;
                    }

                    comboTask = C_IDLE;
                    RECT rect;
                    GetWindowRect(statusDlg, &rect);
                    xPosition = rect.left;
                    yPosition = rect.top;
                    positioned = true;

                    // Extend the dialog by replacing it with a new one created from a different resource.
                    // Resizing wouldn't work, because any resizing causes visible damage to the dialog's background
                    // due to some messages not getting through to repair it
                    StartThread(Control);

                    // Invalidate cache
                    validatedhTxtbox = FALSE;
                }
                break;
            case IDC_PLAY:
                activeCombo = ListBox_GetCurSel(GetDlgItem(statusDlg, IDC_MACROLIST));
                if (activeCombo == -1)
                {
                    set_status("Select a combo first");
                    break;
                }
                set_status("Playing combo");
                CheckDlgButton(statusDlg, IDC_LOOP, 0);
                if (comboTask != C_PAUSE) comboStart = frameCounter;
                comboTask = C_RUNNING;
                break;
            case IDC_STOP:
                set_status("Idle");
                CheckDlgButton(statusDlg, IDC_LOOP, 0);
                comboTask = C_IDLE;
                comboStart = 0; //should avoid unnecessary bugs
                break;
            case IDC_PAUSE:
                if (comboTask == C_RUNNING || comboTask == C_LOOP)
                {
                    set_status("Paused");
                    CheckDlgButton(statusDlg, IDC_LOOP, 0);
                    comboTask = C_PAUSE;
                }
                break;
            case IDC_LOOP:
                if (comboTask == C_LOOP)
                {
                    set_status("Stopped looping");
                    comboTask = C_RUNNING;
                    break;
                }
                activeCombo = ListBox_GetCurSel(GetDlgItem(statusDlg, IDC_MACROLIST));
                if (activeCombo == -1)
                {
                    set_status("Select a combo first");
                    break;
                }
                if (comboTask != C_PAUSE) comboStart = frameCounter;
                comboTask = C_LOOP;
                set_status("Looping combo");
                break;
            case IDC_RECORD:
                if (comboTask == C_RECORD)
                {
                    set_status("Recording stopped");
                    comboTask = C_IDLE;
                    //free(aCombo.data); //is it better to copy to new, aligned buffer or leave it?
                }
                else
                {
                    if (comboTask == C_IDLE || activeCombo == -1)
                    {
                        set_status("Recording new combo");
                        activeCombo = CreateNewCombo(-1);
                        ListBox_SetCurSel(lBox, activeCombo);
                        comboStart = frameCounter;
                    }
                    else
                    {
                        set_status("Overwriting combo");
                        // FIXME: is this right?
                        ACTIVE_COMBO->samples = {};
                    }
                    comboTask = C_RECORD;
                }
                break;
            case IDC_EDIT:
                activeCombo = ListBox_GetCurSel(GetDlgItem(statusDlg, IDC_MACROLIST));
                if (activeCombo == -1)
                {
                    set_status("Select a combo first");
                    break;
                }
                set_status("Editing...");
                StartEdit(activeCombo);
                break;
            case IDC_CLEAR:
                comboTask = C_IDLE;
                activeCombo = -1;
            //good joke, imagine msgbox working
            //if (MessageBox(0, "Do you want to remove everything?", "Warning", MB_YESNO| MB_ICONQUESTION) == 6)
            //{
                set_status("Clearing...");
                CheckDlgButton(statusDlg, IDC_LOOP, 0);
                FreeCombos();
                ListBox_ResetContent(lBox);
                set_status("Cleared all combos");
            //}
                break;
            case IDC_IMPORT:
                {
                    set_status("Importing...");
                    OPENFILENAME data = {0};
                    char file[MAX_PATH] = "\0";
                    data.lStructSize = sizeof(data);
                    data.lpstrFilter = "Combo file (*.cmb)\0*.cmb\0\0";
                    data.nFilterIndex = 1;
                    data.nMaxFile = MAX_PATH;
                    data.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    data.lpstrFile = file;
                    lock = true;
                    if (GetOpenFileName(&data))
                    {
                        load_combos(file);
                    }
                    lock = false;
                    set_status("Imported combo data");
                    break;
                }
            case IDC_SAVE:
                set_status("Saving...");
                SaveCombos();
                set_status("Saved to combos.cmb");
                break;
            default:
                //					SetFocus(statusDlg);
                break;
            }
            break;

        default:
            break;
        }
    return FALSE; //Using DefWindowProc is prohibited but worked anyway
}
