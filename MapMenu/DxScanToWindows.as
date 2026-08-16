class DxScanToWindows {
    static function dxScanCodeToVK(dxScanCode:Number):Number {
        // Create an empty object
        var dxToVK:Object = new Object();

        // Assign properties one by one
        dxToVK[1] = 27;   // Escape
        dxToVK[2] = 49;   // 1
        dxToVK[3] = 50;   // 2
        dxToVK[4] = 51;   // 3
        dxToVK[5] = 52;   // 4
        dxToVK[6] = 53;   // 5
        dxToVK[7] = 54;   // 6
        dxToVK[8] = 55;   // 7
        dxToVK[9] = 56;   // 8
        dxToVK[10] = 57;  // 9
        dxToVK[11] = 48;  // 0
        dxToVK[12] = 189; // Minus
        dxToVK[13] = 187; // Equals
        dxToVK[14] = 8;   // Backspace
        dxToVK[15] = 9;   // Tab
        dxToVK[16] = 81;  // Q
        dxToVK[17] = 38;  // W, same as Up Arrow
        dxToVK[18] = 69;  // E
        dxToVK[19] = 82;  // R
        dxToVK[20] = 84;  // T
        dxToVK[21] = 89;  // Y
        dxToVK[22] = 85;  // U
        dxToVK[23] = 73;  // I
        dxToVK[24] = 79;  // O
        dxToVK[25] = 80;  // P
        dxToVK[26] = 219; // Left Bracket
        dxToVK[27] = 221; // Right Bracket
        dxToVK[28] = 13;  // Enter
        dxToVK[29] = 17;  // Left Control
        dxToVK[30] = 37;  // A, same as Left Arrow
        dxToVK[31] = 40;  // S, same as Down Arrow
        dxToVK[32] = 39;  // D, same as Right Arrow
        dxToVK[33] = 70;  // F
        dxToVK[34] = 71;  // G
        dxToVK[35] = 72;  // H
        dxToVK[36] = 74;  // J
        dxToVK[37] = 75;  // K
        dxToVK[38] = 76;  // L
        dxToVK[39] = 186; // Semicolon
        dxToVK[40] = 222; // Apostrophe
        dxToVK[41] = 192; // ~ (Console)
        dxToVK[42] = 160; // Left Shift
        dxToVK[43] = 220; // Back Slash
        dxToVK[44] = 90;  // Z
        dxToVK[45] = 88;  // X
        dxToVK[46] = 67;  // C
        dxToVK[47] = 86;  // V
        dxToVK[48] = 66;  // B
        dxToVK[49] = 78;  // N
        dxToVK[50] = 77;  // M
        dxToVK[51] = 188; // Comma
        dxToVK[52] = 190; // Period
        dxToVK[53] = 191; // Forward Slash
        dxToVK[54] = 161; // Right Shift
        dxToVK[55] = 106; // NUM*
        dxToVK[56] = 164; // Left Alt
        dxToVK[57] = 32;  // Spacebar
        dxToVK[58] = 20;  // Caps Lock

        // Function Keys
        dxToVK[59] = 112; // F1
        dxToVK[60] = 113; // F2
        dxToVK[61] = 114; // F3
        dxToVK[62] = 115; // F4
        dxToVK[63] = 116; // F5
        dxToVK[64] = 117; // F6
        dxToVK[65] = 118; // F7
        dxToVK[66] = 119; // F8
        dxToVK[67] = 120; // F9
        dxToVK[68] = 121; // F10
        dxToVK[87] = 122; // F11
        dxToVK[88] = 123; // F12

        // Numpad
        dxToVK[71] = 103; // NUM7
        dxToVK[72] = 104; // NUM8
        dxToVK[73] = 105; // NUM9
        dxToVK[74] = 109; // NUM-
        dxToVK[75] = 100; // NUM4
        dxToVK[76] = 101; // NUM5
        dxToVK[77] = 102; // NUM6
        dxToVK[78] = 107; // NUM+
        dxToVK[79] = 97;  // NUM1
        dxToVK[80] = 98;  // NUM2
        dxToVK[81] = 99;  // NUM3
        dxToVK[82] = 96;  // NUM0
        dxToVK[83] = 110; // NUM.
        dxToVK[156] = 13; // NUM Enter

        // Navigation and Special Keys
        dxToVK[69] = 144; // Num Lock
        dxToVK[70] = 145; // Scroll Lock
        dxToVK[157] = 163; // Right Control
        dxToVK[181] = 111; // NUM/
        dxToVK[183] = 44;  // SysRq / PtrScr
        dxToVK[184] = 165; // Right Alt
        dxToVK[197] = 19;  // Pause
        dxToVK[199] = 36;  // Home
        dxToVK[200] = 38;  // Up Arrow
        dxToVK[201] = 33;  // PgUp
        dxToVK[203] = 37;  // Left Arrow
        dxToVK[205] = 39;  // Right Arrow
        dxToVK[207] = 35;  // End
        dxToVK[208] = 40;  // Down Arrow
        dxToVK[209] = 34;  // PgDown
        dxToVK[210] = 45;  // Insert
        dxToVK[211] = 46;  // Delete

        // Mouse Buttons (if needed)
        dxToVK[256] = 1;   // Left Mouse Button
        dxToVK[257] = 2;   // Right Mouse Button
        dxToVK[258] = 4;   // Middle/Wheel Mouse Button
		
		//Gamepad
		dxToVK[276] = 13; // A
		dxToVK[277] = 9; // B
		dxToVK[278] = 98; // X
		dxToVK[279] = 99; // Y
		dxToVK[280] = 101; // LT
		dxToVK[281] = 104; // RT
		dxToVK[274] = 100; // LB
		dxToVK[275] = 103; // RB
		dxToVK[272] = 102; // LS
		dxToVK[273] = 105; // RS

        return dxToVK[dxScanCode] || 0; // Return 0 if not found
    }
}
