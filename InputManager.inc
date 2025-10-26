class InputManager {
public:
    std::set<char> getInputs() {
        std::set<char> inputs;
#ifdef _WIN32
        if (GetAsyncKeyState('W') & 0x8000) inputs.insert('w');
        if (GetAsyncKeyState('A') & 0x8000) inputs.insert('a');
        if (GetAsyncKeyState('S') & 0x8000) inputs.insert('s');
        if (GetAsyncKeyState('D') & 0x8000) inputs.insert('d');
        if (GetAsyncKeyState('Q') & 0x8000) inputs.insert('q');
        if (GetAsyncKeyState(VK_SPACE) & 0x8000) inputs.insert(' ');
#else
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        int bytesWaiting;
        ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting);
        while (bytesWaiting-- > 0) {
            char ch = 0;
            read(STDIN_FILENO, &ch, 1);
            if (ch == 'w' || ch == 'a' || ch == 's' || ch == 'd' || ch == 'q' || ch == ' ')
                inputs.insert(ch);
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
        return inputs;
    }
};