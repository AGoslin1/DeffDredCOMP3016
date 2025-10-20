#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <random>
#include <limits>
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

constexpr int GRID_ROWS = 20;
constexpr int GRID_COLS = 60;
constexpr int FRAME_MS = 60;

enum class UpgradeType {
    IncreaseHP,
    AttackSpeed,
    BulletSpeed,
    Damage,
    MoveSpeed,
    BulletsAmount,
    LifeSteal
};

static const char* GetUpgradeName(UpgradeType upg) {
    switch (upg) {
    case UpgradeType::IncreaseHP:    return "Increase Max HP (+5)";
    case UpgradeType::AttackSpeed:   return "Attack Speed (+20%)";
    case UpgradeType::BulletSpeed:   return "Bullet Speed (+1)";
    case UpgradeType::Damage:        return "Damage (+5)";
    case UpgradeType::MoveSpeed:     return "Move Speed (+1)";
    case UpgradeType::BulletsAmount: return "Bullets Amount (+1 stream, up to 8)";
    case UpgradeType::LifeSteal:     return "Life Steal (+5% per upgrade)";
    default:                         return "Unknown";
    }
}

class Bullet {
public:
    int x, y, dx, dy;
    char symbol;
    Bullet(int x_, int y_, int dx_, int dy_, char symbol_ = '*')
        : x(x_), y(y_), dx(dx_), dy(dy_), symbol(symbol_) {
    }
    void update() { x += dx; y += dy; }
    bool isOutOfBounds() const {
        return x < 0 || x >= GRID_COLS || y < 0 || y >= GRID_ROWS;
    }
};

class Player {
public:
    int x, y;
    int hp = 10;
    int maxHp = 10;
    int money = 0;
    int maxMoney = 100;
    int fireCooldownMs = 500;
    int bulletSpeed = -1;
    int damage = 10;
    int moveSpeed = 1;
    int bulletStreams = 1; //number of active streams (1-8)
    int lifeStealPercent = 0;
    const std::vector<std::string> shape = { " A ","/V\\" };
    Player(int x_, int y_) : x(x_), y(y_) {}
    void move(const std::set<char>& inputs) {
        if (inputs.count('w') && y > 0) y -= moveSpeed;
        if (inputs.count('s') && y < GRID_ROWS - 2) y += moveSpeed;
        if (inputs.count('a') && x > 0) x -= moveSpeed;
        if (inputs.count('d') && x < GRID_COLS - 3) x += moveSpeed;
        if (x < 0) x = 0;
        if (x > GRID_COLS - 3) x = GRID_COLS - 3;
        if (y < 0) y = 0;
        if (y > GRID_ROWS - 2) y = GRID_ROWS - 2;
    }
    bool collides(const Bullet& b) const {
        for (int dy = 0; dy < shape.size(); ++dy) {
            for (int dx = 0; dx < shape[dy].size(); ++dx) {
                if (shape[dy][dx] != ' ' &&
                    b.x == x + dx && b.y == y + dy)
                    return true;
            }
        }
        return false;
    }
};

struct BulletSpawn {
    int time, x, y, dx, dy;
};

class BulletManager {
    std::vector<BulletSpawn> spawns;
    size_t nextSpawn = 0;
public:
    void loadPattern(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) throw std::runtime_error("Pattern file not found");
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            BulletSpawn spawn;
            if (!(iss >> spawn.time >> spawn.x >> spawn.y >> spawn.dx >> spawn.dy))
                throw std::runtime_error("Invalid pattern file format");
            spawns.push_back(spawn);
        }
        std::sort(spawns.begin(), spawns.end(), [](const BulletSpawn& a, const BulletSpawn& b) {
            return a.time < b.time;
            });
    }
    void spawnBullets(int frame, std::vector<std::unique_ptr<Bullet>>& bullets) {
        while (nextSpawn < spawns.size() && spawns[nextSpawn].time <= frame) {
            const auto& s = spawns[nextSpawn];
            bullets.push_back(std::make_unique<Bullet>(s.x, s.y, s.dx, s.dy));
            nextSpawn++;
        }
    }
};

class Enemy {
public:
    int x, y;
    int hp = 10;
    int maxHp = 10;
    int fireCooldown = 60;
    int fireTimer = 0;
    int dx = 0, dy = 0;
    int moveTimer = 0;
    int moveFrameCounter = 0;
    int burstSteps = 0;
    int pauseTimer = 0;
    std::vector<std::string> shape;
    Enemy(int x_, int y_) : x(x_), y(y_), shape({ "#" }) {}
    virtual ~Enemy() {}
    virtual void update() {
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_int_distribution<int> dirDist(-1, 1);

        if (pauseTimer > 0) {
            pauseTimer--;
            if (fireTimer > 0) fireTimer--;
            return;
        }
        if (burstSteps <= 0) {
            dx = dirDist(rng);
            dy = dirDist(rng);
            burstSteps = 5;
            pauseTimer = 16;
        }
        moveFrameCounter++;
        if (moveFrameCounter >= 3) {
            x += dx;
            y += dy;
            moveFrameCounter = 0;
            burstSteps--;
        }
        if (x < 0) x = 0;
        if (x > GRID_COLS - 1) x = GRID_COLS - 1;
        if (y < 0) y = 0;
        if (y > GRID_ROWS - 1) y = GRID_ROWS - 1;
        if (fireTimer > 0) fireTimer--;
    }
    virtual bool canFire() const { return fireTimer == 0; }
    virtual void resetFire() { fireTimer = fireCooldown; }
    virtual bool isRayEnemy() const { return false; }
    bool isAlive() const { return hp > 0; }
};

class RayEnemy : public Enemy {
public:
    enum class State { Cooldown, Flashing, Firing };

    static constexpr int FLASH_FRAMES = (1000 + FRAME_MS - 1) / FRAME_MS;
    static constexpr int FIRE_FRAMES  = (1500 + FRAME_MS - 1) / FRAME_MS;
    static constexpr int COOLDOWN_FRAMES = (4000 + FRAME_MS - 1) / FRAME_MS;

    static constexpr int DAMAGE = 2; // damage dealt once per firing cycle

    State state = State::Cooldown;
    int timer = COOLDOWN_FRAMES;
    bool playerDamagedThisFire = false;

    RayEnemy(int x_, int y_) : Enemy(x_, y_) {
        shape = { "@" };
        hp = maxHp = 20;
    }

    void update() override {
        switch (state) {
            case State::Cooldown:
                Enemy::update();
                if (--timer <= 0) {
                    state = State::Flashing;
                    timer = FLASH_FRAMES;
                }
                break;
            case State::Flashing:
                if (--timer <= 0) {
                    state = State::Firing;
                    timer = FIRE_FRAMES;
                    playerDamagedThisFire = false;
                }
                break;
            case State::Firing:
                if (--timer <= 0) {
                    state = State::Cooldown;
                    timer = COOLDOWN_FRAMES;
                    playerDamagedThisFire = false;
                }
                break;
        }
    }

    bool canFire() const override { return false; }
    void resetFire() override {}
    bool isRayEnemy() const override { return true; }

    bool isFlashing() const { return state == State::Flashing; }
    bool isFiring() const { return state == State::Firing; }
};

class Boss : public Enemy {
public:
    Boss(int x_, int y_) : Enemy(x_, y_) {
        shape = { "<#>", " V" };
        hp = maxHp = 150;
        fireCooldown = 60;
    }
};

class Renderer {
public:
    void draw(const Player& player, const std::vector<std::unique_ptr<Enemy>>& enemies, const std::vector<std::unique_ptr<Bullet>>& bullets, int frame) {
        int barWidth = GRID_COLS;

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
            for (int dx = 0; dx < static_cast<int>(row.size()); ++dx) {
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
                int ex = ray->x + 1, ey = ray->y + 1;
                if (ey >= 1 && ey <= GRID_ROWS && ex >= 1 && ex <= GRID_COLS)
                    grid[ey][ex] = '@';

                if (ray->isFlashing()) {
                    //telegraph with single line cross for clarity
                    if ((frame / 4) % 2 == 0) {
                        if (ex >= 1 && ex <= GRID_COLS) {
                            for (int y = 1; y <= GRID_ROWS; ++y)
                                grid[y][ex] = '|';
                        }
                        if (ey >= 1 && ey <= GRID_ROWS) {
                            for (int x = 1; x <= GRID_COLS; ++x)
                                grid[ey][x] = '-';
                        }
                    }
                } else if (ray->isFiring()) {
                    //3 wide cross spanning lazers the entire arena
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

class Game {
    Player player;
    BulletManager bulletManager;
    Renderer renderer;
    InputManager inputManager;
    std::vector<std::unique_ptr<Bullet>> bullets;
    std::vector<std::unique_ptr<Enemy>> enemies;
    int frame = 0;
    bool running = true;
    std::chrono::steady_clock::time_point lastPlayerBulletTime;
    int enemySpawnFrameCounter = 0;     // RayEnemy spawn counter (200 frames)
    int basicSpawnFrameCounter = 0;     // Basic enemy spawn counter (166 frames)
    bool upgradePending = false;
    std::vector<UpgradeType> offeredUpgrades;
    int score = 0;
public:
    Game() : player(GRID_COLS / 2 - 1, GRID_ROWS - 4) {}

    void run(const std::string& patternFile) {
        try {
            bulletManager.loadPattern(patternFile);
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading pattern: " << e.what() << "\nStarting empty level.\n";
        }
        enemies.push_back(std::make_unique<Enemy>(GRID_COLS / 2 - 1, 2));
        enemies.push_back(std::make_unique<RayEnemy>(GRID_COLS / 2 - 1, GRID_ROWS / 2));

        lastPlayerBulletTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(500);

        while (running) {
            auto start = std::chrono::steady_clock::now();
            std::set<char> inputs = inputManager.getInputs();
            if (inputs.count('q')) break;
            player.move(inputs);
            bulletManager.spawnBullets(frame, bullets);
            for (auto& b : bullets) b->update();
            bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
                [](const std::unique_ptr<Bullet>& b) { return b->isOutOfBounds(); }), bullets.end());
            for (const auto& b : bullets) {
                if ((b->symbol == '*' || b->symbol == 'O') && player.collides(*b)) {
                    int dmg = (b->symbol == 'O') ? 3 : 1;
                    int newHp = player.hp - dmg;
                    if (newHp < 0) newHp = 0;
                    player.hp = newHp;
                    if (player.hp <= 0) {
                        running = false;
                        break;
                    }
                }
            }
            for (auto& enemyPtr : enemies) {
                if (!enemyPtr->isAlive()) continue;
                enemyPtr->update();

                if (enemyPtr->canFire()) {
                    if (Boss* boss = dynamic_cast<Boss*>(enemyPtr.get())) {
                        const int cx = boss->x + 1;
                        const int cy = boss->y + 0; // centre row and column

                        const int dirs[16][2] = {
                            {  0, -2 }, {  2,  0 }, {  0,  2 }, { -2,  0 },
                            {  2, -2 }, {  2,  2 }, { -2,  2 }, { -2, -2 },
                            {  2, -1 }, {  1, -2 }, {  2,  1 }, {  1,  2 },
                            { -2,  1 }, { -1,  2 }, { -2, -1 }, { -1, -2 }
                        };

                        for (int i = 0; i < 16; ++i) {
                            int dx = dirs[i][0];
                            int dy = dirs[i][1];
                            if (cx >= 0 && cx < GRID_COLS && cy >= 0 && cy < GRID_ROWS) {
                                bullets.push_back(std::make_unique<Bullet>(cx, cy, dx, dy, 'O'));
                            }
                        }
                        enemyPtr->resetFire();
                        continue;
                    }

                    //default enemy fire aiming a single * at the player
                    int px = player.x + 1, py = player.y + 1;
                    int ex = enemyPtr->x + 1, ey = enemyPtr->y + 1;
                    int dx = px - ex;
                    int dy = py - ey;
                    if (dx < 0) dx = -1;
                    else if (dx > 0) dx = 1;
                    else dx = 0;
                    if (dy < 0) dy = -1;
                    else if (dy > 0) dy = 1;
                    else dy = 0;
                    if (py > ey) dy = 1;
                    bullets.push_back(std::make_unique<Bullet>(enemyPtr->x, enemyPtr->y + 1, dx, dy, '*'));
                    enemyPtr->resetFire();
                }
            }
            //player bullets damage enemies (with life steal and single death reward)
            for (auto& enemyPtr : enemies) {
                if (!enemyPtr->isAlive()) continue;

                bool enemyDied = false;
                for (auto& b : bullets) {
                    if (enemyDied) break;
                    if (b->symbol != 'o') continue;

                    bool damaged = false;
                    for (int sy = 0; sy < static_cast<int>(enemyPtr->shape.size()) && !damaged; ++sy) {
                        for (int sx = 0; sx < static_cast<int>(enemyPtr->shape[sy].size()) && !damaged; ++sx) {
                            if (enemyPtr->shape[sy][sx] == ' ') continue;

                            const int ex = enemyPtr->x + sx;
                            const int ey = enemyPtr->y + sy;

							//hit if bullet is on the enemy cell or in any adjacent spot
                            if ((b->x > ex ? b->x - ex : ex - b->x) + (b->y > ey ? b->y - ey : ey - b->y) <= 1) {
                                const int beforeHp = enemyPtr->hp;

                                int dmg = player.damage;
                                if (dmg < 0) dmg = 0;
                                int dealt = beforeHp < dmg ? beforeHp : dmg;
                                if (dealt < 0) dealt = 0;

                                enemyPtr->hp = enemyPtr->hp - dmg;
                                b->x = -100; //mark bullet for removal
                                damaged = true;

                                if (dealt > 0 && player.lifeStealPercent > 0) {
                                    int heal = (dealt * player.lifeStealPercent) / 100;
                                    if (heal > 0) {
                                        int newHp = player.hp + heal;
                                        player.hp = newHp > player.maxHp ? player.maxHp : newHp;
                                    }
                                }

                                //award money and score if alive
                                if (beforeHp > 0 && enemyPtr->hp <= 0) {
                                    player.money += 10;
                                    score += 50;
                                    enemyDied = true;
                                }
                            }
                        }
                    }
                }
            }
            for (auto& enemyPtr : enemies) {
                RayEnemy* ray = dynamic_cast<RayEnemy*>(enemyPtr.get());
                if (ray && ray->isFiring() && enemyPtr->isAlive()) {
                    int ex = ray->x, ey = ray->y;
                    // Vertical ray
                    if (player.x + 1 == ex + 1 && std::abs(player.y + 1 - (ey + 1)) <= 3) {
                        player.hp = 0;
                        running = false;
                    }
                    // Horizontal ray
                    if (player.y + 1 == ey + 1 && std::abs(player.x + 1 - (ex + 1)) <= 8) {
                        player.hp = 0;
                        running = false;
                    }
                }
            }
            // RayEnemy collision: during firing, player touching the 3-wide cross is hit once per firing cycle
            for (auto& enemyPtr : enemies) {
                RayEnemy* ray = dynamic_cast<RayEnemy*>(enemyPtr.get());
                if (!ray || !enemyPtr->isAlive() || !ray->isFiring()) continue;
                if (ray->playerDamagedThisFire) continue; // already applied this cycle

                int ex = ray->x;
                int ey = ray->y;

                bool hit = false;
                for (int pdy = 0; pdy < static_cast<int>(player.shape.size()) && !hit; ++pdy) {
                    for (int pdx = 0; pdx < static_cast<int>(player.shape[pdy].size()) && !hit; ++pdx) {
                        if (player.shape[pdy][pdx] == ' ') continue;
                        int px = player.x + pdx;
                        int py = player.y + pdy;
                        if ((px >= ex - 1 && px <= ex + 1) || (py >= ey - 1 && py <= ey + 1)) {
                            hit = true;
                        }
                    }
                }
                if (hit) {
                    int newHp = player.hp - RayEnemy::DAMAGE;
                    if (newHp < 0) newHp = 0;
                    player.hp = newHp;
                    ray->playerDamagedThisFire = true;
                    if (player.hp <= 0) {
                        running = false;
                        break;
                    }
                }
            }
            if (!upgradePending && player.money >= player.maxMoney) {
                offerUpgrades();
            }
            if (upgradePending) {
                renderer.clearScreen();
                std::cout << "Choose an upgrade:\n";
                for (int i = 0; i < 3; ++i) {
                    std::cout << (i + 1) << ". " << GetUpgradeName(offeredUpgrades[i]) << "\n";
                }
                std::cout << "Press 1, 2, or 3 to select.\n";
                int choice = 0;
                while (choice < 1 || choice > 3) {
                    char ch;
#ifdef _WIN32
                    ch = _getch();
#else
                    std::cin >> ch;
#endif
                    choice = ch - '0';
                }
                applyUpgrade(offeredUpgrades[choice - 1]);
                player.money = 0;
                upgradePending = false;
                continue;
            }
            renderer.draw(player, enemies, bullets, frame);
            std::cout << "Frame: " << frame << " | Use WASD to move, Q to quit\n";
            if (inputs.count(' ')) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPlayerBulletTime).count() >= player.fireCooldownMs) {
                    int bulletX = player.x + 1;
                    int bulletY = player.y;
                    int spd = (player.bulletSpeed < 0) ? -player.bulletSpeed : player.bulletSpeed;

                    std::vector<std::pair<int,int>> dirs;
                    dirs.push_back({ 0, player.bulletSpeed });

                    if (player.bulletStreams >= 2) dirs.push_back({ -1, player.bulletSpeed }); //up left
                    if (player.bulletStreams >= 3) dirs.push_back({ +1, player.bulletSpeed }); //up right
                    if (player.bulletStreams >= 4) dirs.push_back({ -spd, 0 });                //left
                    if (player.bulletStreams >= 5) dirs.push_back({ +spd, 0 });                //right
                    if (player.bulletStreams >= 6) dirs.push_back({ -1, +spd });               //down left
                    if (player.bulletStreams >= 7) dirs.push_back({ +1, +spd });               //down right
                    if (player.bulletStreams >= 8) dirs.push_back({ 0, +spd });                //down

                    for (const auto& d : dirs) {
                        if (bulletX >= 0 && bulletX < GRID_COLS && bulletY >= 0 && bulletY < GRID_ROWS) {
                            bullets.push_back(std::make_unique<Bullet>(bulletX, bulletY, d.first, d.second, 'o'));
                        }
                    }

                    lastPlayerBulletTime = now;
                }
            }
            frame++;
            enemySpawnFrameCounter++;
            basicSpawnFrameCounter++;

            //+50 score every 50 frames survived
            if (frame > 0 && (frame % 50) == 0) {
                score += 50;
            }

            //7.5% decrease every 100 frames after 1500 frames
            const int basicBaseInterval = 83;
            const int rayBaseInterval   = 100; //current baseline

            int basicInterval = basicBaseInterval;
            int rayInterval   = rayBaseInterval;

            int overFrames = frame - 1500;
            if (overFrames > 0) {
                int steps = overFrames / 100;


                double b = static_cast<double>(basicInterval);
                double r = static_cast<double>(rayInterval);
                for (int i = 0; i < steps; ++i) {
                    b *= 0.925;
                    r *= 0.925;
                }
                basicInterval = static_cast<int>(b + 0.5);
                rayInterval   = static_cast<int>(r + 0.5);
                if (basicInterval < 1) basicInterval = 1;
                if (rayInterval   < 1) rayInterval   = 1;
            }

            if (basicSpawnFrameCounter >= basicInterval) {
                static std::mt19937 rng(std::random_device{}());
                std::uniform_int_distribution<int> xDist(0, GRID_COLS - 1);
                std::uniform_int_distribution<int> yDist(0, 2);
                int ex = xDist(rng);
                int ey = yDist(rng);
                enemies.push_back(std::make_unique<Enemy>(ex, ey));
                basicSpawnFrameCounter = 0;
            }

            if (enemySpawnFrameCounter >= rayInterval) {
                static std::mt19937 rngRay(std::random_device{}());
                std::uniform_int_distribution<int> xDistRay(0, GRID_COLS - 1);
                int ex = xDistRay(rngRay);
                int ey = GRID_ROWS / 2;
                enemies.push_back(std::make_unique<RayEnemy>(ex, ey));
                enemySpawnFrameCounter = 0;
            }

            //spawn boss at frame 2250 and every 500 frames after
            if (frame >= 2250 && ((frame - 2250) % 500 == 0)) {
                int bx = GRID_COLS / 2 - 1;
                int by = 1;
                enemies.push_back(std::make_unique<Boss>(bx, by));
            }

            std::this_thread::sleep_until(start + std::chrono::milliseconds(FRAME_MS));
        }
        //on death prompt for username, store score, and show leaderboard
        renderer.clearScreen();
        std::cout << "Game Over! Survived " << frame << " frames.\n";
        std::cout << "Your score: " << score << "\n\n";
        std::cout << "Enter a username for the leaderboard: " << std::flush;

#ifdef _WIN32
{
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE) {
        FlushConsoleInputBuffer(hIn);
    }
}
#else
tcflush(STDIN_FILENO, TCIFLUSH);
#endif

std::cin.clear();
while (std::cin.rdbuf()->in_avail() > 0) {
    std::cin.get();
}

        std::string username;
        std::getline(std::cin, username);

        for (size_t i = 0; i < username.size(); ++i) {
            if (username[i] == '\t' || username[i] == '\r' || username[i] == '\n') {
                username[i] = ' ';
            }
        }
        if (username.size() > 24) username.resize(24);
        if (username.empty()) username = "Player";

        //append score to highscores.txt
        const char* highscoresPath = "highscores.txt";
        {
            std::ofstream appendFile(highscoresPath, std::ios::app);
            if (appendFile) {
                appendFile << score << '\t' << username << '\n';
            } else {
                std::cerr << "Warning: could not open highscores.txt for writing.\n";
            }
        }

        // Load, sort, and display Top 10
        std::vector<std::pair<int, std::string>> allScores;
        {
            std::ifstream in(highscoresPath);
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                size_t sep = line.find('\t');
                if (sep == std::string::npos) continue;
                std::string sPart = line.substr(0, sep);
                std::string nPart = line.substr(sep + 1);
                if (!nPart.empty() && nPart.back() == '\r') nPart.pop_back();

                int sVal = 0;
                try {
                    sVal = std::stoi(sPart);
                } catch (...) {
                    continue;
                }
                allScores.emplace_back(sVal, nPart);
            }
        }

        //sort descending by score
        std::sort(allScores.begin(), allScores.end(),
            [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
                return a.first > b.first;
            });

        //show leaderboard
        renderer.clearScreen();
        std::cout << "===== Leaderboard (Top 10) =====\n";
        int topCount = static_cast<int>(allScores.size());
        if (topCount > 10) topCount = 10;
        for (int i = 0; i < topCount; ++i) {
            std::cout << (i + 1) << ". " << allScores[i].second << " - " << allScores[i].first << "\n";
        }
        std::cout << "\nYour score: " << score << "\n";
    }

    void offerUpgrades() {
        std::vector<UpgradeType> allUpgrades = {
            UpgradeType::IncreaseHP, UpgradeType::AttackSpeed,
            UpgradeType::BulletSpeed, UpgradeType::Damage,
            UpgradeType::MoveSpeed, UpgradeType::BulletsAmount,
            UpgradeType::LifeSteal // NEW
        };
        std::shuffle(allUpgrades.begin(), allUpgrades.end(), std::mt19937(std::random_device{}()));
        offeredUpgrades.assign(allUpgrades.begin(), allUpgrades.begin() + 3);
        upgradePending = true;
    }

    void applyUpgrade(UpgradeType upg) {
        switch (upg) {
        case UpgradeType::IncreaseHP:
            player.maxHp += 5; player.hp += 5; break;
        case UpgradeType::AttackSpeed:
            player.fireCooldownMs = (player.fireCooldownMs * 0.8); break;
        case UpgradeType::BulletSpeed:
            player.bulletSpeed -= 1; break;
        case UpgradeType::Damage:
            player.damage += 5; break;
        case UpgradeType::MoveSpeed:
            player.moveSpeed += 1; break;
        case UpgradeType::BulletsAmount: {
            if (player.bulletStreams < 8) player.bulletStreams++;
            break;
        }
        case UpgradeType::LifeSteal:
            player.lifeStealPercent += 5;
            break;
        }
    }
};

int main() {
    Game game;
    game.run("pattern.txt");
    return 0;
}