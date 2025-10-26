class Renderer {
public:
    void draw(const Player& player, const std::vector<std::unique_ptr<Enemy>>& enemies, const std::vector<std::unique_ptr<Bullet>>& bullets, int frame) {
        int barWidth = GRID_COLS;

        // Clamp HP
        int clampedHp = player.hp;
        if (clampedHp < 0) clampedHp = 0;
        if (clampedHp > player.maxHp) {
            clampedHp = player.maxHp;
        }

        int hpBarLength = 0;
        if (player.maxHp > 0) {
            hpBarLength = (clampedHp * barWidth) / player.maxHp;
        }
        if (hpBarLength < 0) hpBarLength = 0;
        if (hpBarLength > barWidth) {
            hpBarLength = barWidth;
        }
        int hpSpaces = barWidth - hpBarLength;
        if (hpSpaces < 0) hpSpaces = 0;

        // Clamp Money
        int clampedMoney = player.money;
        if (clampedMoney < 0) clampedMoney = 0;
        if (clampedMoney > player.maxMoney) {
            clampedMoney = player.maxMoney;
        }

        int moneyBarLength = 0;
        if (player.maxMoney > 0) {
            moneyBarLength = (clampedMoney * barWidth) / player.maxMoney;
        }
        if (moneyBarLength < 0) moneyBarLength = 0;
        if (moneyBarLength > barWidth) moneyBarLength = barWidth;
        int moneySpaces = barWidth - moneyBarLength;
        if (moneySpaces < 0) moneySpaces = 0;

        std::string hpBar = std::string("HP: [") + std::string(hpBarLength, '#') + std::string(hpSpaces, ' ') + "]";
        std::string moneyBar = std::string("$:  [") + std::string(moneyBarLength, '#') + std::string(moneySpaces, ' ') + "]";

        clearScreen();
        std::cout << hpBar << "\n" << moneyBar << "\n";

        std::vector<std::string> grid(GRID_ROWS + 2, std::string(GRID_COLS + 2, ' '));
        grid[0][0] = grid[GRID_ROWS + 1][0] = '+';
        grid[0][GRID_COLS + 1] = grid[GRID_ROWS + 1][GRID_COLS + 1] = '+';
        for (int x = 1; x <= GRID_COLS; ++x) {
            grid[0][x] = '-';
            grid[GRID_ROWS + 1][x] = '-';
        }
        for (int y = 1; y <= GRID_ROWS; ++y) {
            grid[y][0] = '|';
            grid[y][GRID_COLS + 1] = '|';
        }
        // Draw player ship
        for (int dy = 0; dy < static_cast<int>(player.shape.size()); ++dy) {
            const std::string& row = player.shape[dy];
            for (int dx = 0; dx < static_cast<int>(row.size()); ++dx) { // NOTE: ++dx (not ++dy)
                char c = row[dx];
                int px = player.x + dx + 1, py = player.y + dy + 1;
                if (c != ' ' && px >= 1 && px <= GRID_COLS && py >= 1 && py <= GRID_ROWS)
                    grid[py][px] = c;
            }
        }
        // Draw all enemies
        for (const auto& enemyPtr : enemies) {
            if (!enemyPtr->isAlive()) continue;
            RayEnemy* ray = dynamic_cast<RayEnemy*>(enemyPtr.get());
            if (ray) {
                int ex = ray->x + 1, ey = ray->y + 1; // +1 border offset
                // Always draw the '@'
                if (ey >= 1 && ey <= GRID_ROWS && ex >= 1 && ex <= GRID_COLS)
                    grid[ey][ex] = '@';

                if (ray->isFlashing()) {
                    // Telegraph (flash) with single-line cross for clarity
                    if ((frame / 4) % 2 == 0) {
                        // Single vertical line at ex spanning full height
                        if (ex >= 1 && ex <= GRID_COLS) {
                            for (int y = 1; y <= GRID_ROWS; ++y)
                                grid[y][ex] = '|';
                        }
                        // Single horizontal line at ey spanning full width
                        if (ey >= 1 && ey <= GRID_ROWS) {
                            for (int x = 1; x <= GRID_COLS; ++x)
                                grid[ey][x] = '-';
                        }
                    }
                } else if (ray->isFiring()) {
                    // Active lasers: 3-wide cross spanning the entire arena
                    for (int xoff = -1; xoff <= 1; ++xoff) {
                        int x = ex + xoff;
                        if (x < 1 || x > GRID_COLS) continue;
                        for (int y = 1; y <= GRID_ROWS; ++y)
                            grid[y][x] = '|';
                    }
                    for (int yoff = -1; yoff <= 1; ++yoff) {
                        int y = ey + yoff;
                        if (y < 1 || y > GRID_ROWS) continue;
                        for (int x = 1; x <= GRID_COLS; ++x)
                            grid[y][x] = '-';
                    }
                }
                continue;
            }
            // Normal enemy drawing
            for (int dy = 0; dy < static_cast<int>(enemyPtr->shape.size()); ++dy) {
                const std::string& erow = enemyPtr->shape[dy];
                for (int dx = 0; dx < static_cast<int>(erow.size()); ++dx) { // FIX: ++dx
                    char c = erow[dx];
                    int ex2 = enemyPtr->x + dx + 1, ey2 = enemyPtr->y + dy + 1;
                    if (c != ' ' && ex2 >= 1 && ex2 <= GRID_COLS && ey2 >= 1 && ey2 <= GRID_ROWS)
                        grid[ey2][ex2] = c;
                }
            }
        }
        // Draw bullets
        for (const auto& b : bullets) {
            int bx = b->x + 1, by = b->y + 1;
            if (bx >= 1 && bx <= GRID_COLS && by >= 1 && by <= GRID_ROWS)
                grid[by][bx] = b->symbol;
        }
        for (const auto& row : grid)
            std::cout << row << '\n';
    }
    void clearScreen() {
#ifdef _WIN32
        system("cls");
#else
        std::cout << "\033[2J\033[1;1H";
#endif
    }
};