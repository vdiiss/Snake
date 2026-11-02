// =====================================================================
//  Домашнее задание 1. «Змейка на C++»
//  Часть II. Вариант 6 «Сад»
//
//  Змейка собирает фрукты трёх типов:
//    яблоко (A) — +1 очко,
//    банан  (B) — +2 очка,
//    вишня  (C) — +3 очка, но исчезает через 3 хода, если её не съесть.
//  Цель — собрать урожай: набрать 30 очков. Стена или свой хвост — проигрыш.
//  Одно нажатие клавиши направления — один ход змейки.
//
//  Сборка (VS Code + WSL): g++ -std=c++17 -Wall -Wextra -o garden variant_06/garden.cpp
//  Запуск:                 ./garden            (или ./garden --seed 42)
// =====================================================================

#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

using namespace std;

// ---------------------------------------------------------------------
//  Параметры игры
// ---------------------------------------------------------------------
const int WIDTH        = 10;              // ширина поля, клеток
const int HEIGHT       = 10;              // высота поля, клеток
const int MAX_LEN      = WIDTH * HEIGHT;  // длиннее числа клеток змейка быть не может
const int TARGET_SCORE = 30;              // столько очков нужно для победы

// Фрукты (индекс — значение FruitType)
enum FruitType { APPLE, BANANA, CHERRY };
const int         FRUIT_TYPES        = 3;
const char* const FRUIT_NAME[]       = {"яблоко", "банан", "вишня"};
const char* const FRUIT_NAME_GEN[]   = {"яблок", "бананов", "вишен"};  // «съедено N ...»
const int         FRUIT_POINTS[]     = {1, 2, 3};

const int REGULAR_FRUITS  = 2;               // сколько яблок/бананов лежит на поле одновременно
const int MAX_FRUITS      = REGULAR_FRUITS + 1;
const int CHERRY_SLOT     = REGULAR_FRUITS;  // последнее место в массиве фруктов — для вишни
const int BANANA_CHANCE   = 35;  // %: вероятность, что новый обычный фрукт окажется бананом
const int CHERRY_CHANCE   = 20;  // %: вероятность появления вишни на очередном ходу
const int CHERRY_LIFETIME = 3;   // через столько ходов несъеденная вишня исчезает
const int CHERRY_RADIUS   = 3;   // вишня появляется не дальше 3 клеток от головы, чтобы успеть

// Действия игрока. Первые пять значений — направления движения,
// поэтому они служат индексами в массивах DX, DY и OPPOSITE.
enum Action { ACT_NONE, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_HELP, ACT_QUIT };

const int    DX[]       = {0,  0, 0, -1, 1};  // смещение головы по X
const int    DY[]       = {0, -1, 1,  0, 0};  // смещение головы по Y (ось направлена вниз)
const Action OPPOSITE[] = {ACT_NONE, ACT_DOWN, ACT_UP, ACT_RIGHT, ACT_LEFT};

// Содержимое клетки поля и его вид на экране (индекс — значение Cell).
// Фрукты идут подряд в том же порядке, что и FruitType: CELL_APPLE + type.
enum Cell { CELL_EMPTY, CELL_HEAD, CELL_BODY, CELL_APPLE, CELL_BANANA, CELL_CHERRY };
const char* const CELL_SYMBOL[] = {".", "@", "o", "A", "B", "C"};
const char* const CELL_COLOR[]  = {"\033[90m", "\033[1;32m", "\033[32m",
                                   "\033[1;31m", "\033[1;33m", "\033[1;35m"};

const char* const WALL_COLOR  = "\033[33m";
const char* const WIN_COLOR   = "\033[1;32m";
const char* const LOSE_COLOR  = "\033[1;31m";
const char* const RESET_COLOR = "\033[0m";

// Чем закончилась игра
enum Result { RES_PLAYING, RES_WALL, RES_TAIL, RES_WIN, RES_QUIT };

// Фрукт на поле
struct Fruit {
    bool      active;  // лежит ли фрукт на поле
    FruitType type;
    int       x, y;
    int       ttl;     // только для вишни: сколько ходов осталось до исчезновения
};

// Состояние игры
struct Game {
    int    x[MAX_LEN];                // координаты сегментов змейки; x[0], y[0] — голова
    int    y[MAX_LEN];
    int    length;                    // текущая длина змейки
    Action dir;                       // направление последнего хода
    Fruit  fruits[MAX_FRUITS];        // [0..REGULAR_FRUITS-1] — яблоки/бананы, [CHERRY_SLOT] — вишня
    int    score;                     // счёт
    int    moves;                     // сколько ходов сделано
    int    eaten[FRUIT_TYPES];        // сколько съедено фруктов каждого типа
    int    cherriesMissed;            // сколько вишен исчезло несъеденными
    Result result;
};

// ---------------------------------------------------------------------
//  Работа с терминалом
// ---------------------------------------------------------------------
termios g_savedTerm;              // настройки терминала до запуска игры
bool    g_rawMode      = false;   // включён режим «клавиша без Enter»
bool    g_interactive  = false;   // игра идёт с клавиатуры, а не по сценарию из файла
bool    g_colors       = false;   // вывод идёт в терминал — можно раскрашивать
bool    g_cursorHidden = false;

// Возвращает терминал в обычный режим и показывает курсор
void restoreTerminal() {
    if (g_rawMode) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
        g_rawMode = false;
    }
    if (g_cursorHidden) {
        cout << "\033[?25h" << flush;
        g_cursorHidden = false;
    }
}

// Включает режим, в котором нажатая клавиша сразу попадает в программу
// (без Enter и без эха на экране)
void setupTerminal() {
    g_interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    g_colors      = isatty(STDOUT_FILENO);

    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_savedTerm) == 0) {
        termios raw = g_savedTerm;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);  // ISIG выключен: Ctrl+C обрабатываем сами
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_rawMode = true;
        atexit(restoreTerminal);
    }
    if (g_interactive) {
        cout << "\033[?25l";  // прячем курсор, чтобы он не мигал на поле
        g_cursorHidden = true;
    }
}

// Начало нового кадра. При игре по сценарию (ввод из файла) экран не очищается:
// кадры идут друг за другом, и видна вся история ходов.
string clearScreen() {
    return g_interactive ? "\033[H\033[2J" : "\n";
}

// Раскрашивает текст, если вывод идёт в терминал
string paint(const string& text, const char* color) {
    return g_colors ? color + text + RESET_COLOR : text;
}

// Читает один байт ввода, ожидая не дольше timeoutMs миллисекунд (-1 — без ограничения).
// Возвращает -1, если ввод закончился или байт не пришёл вовремя.
int readByte(int timeoutMs) {
    pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    int ready;
    do {
        ready = poll(&pfd, 1, timeoutMs);
    } while (ready < 0 && errno == EINTR);
    if (ready <= 0) return -1;

    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    return c;
}

// Ждёт нажатия клавиши и переводит его в действие игрока.
// Понимает WASD, стрелки и те же клавиши в русской раскладке (ЦФЫВ).
Action readAction() {
    int c = readByte(-1);
    if (c < 0) return ACT_QUIT;  // ввод закончился
    if (c == 3) return ACT_QUIT; // Ctrl+C

    // Стрелки присылают последовательность ESC [ A..D (или ESC O A..D)
    if (c == 27) {
        int c1 = readByte(50);
        if (c1 == '[' || c1 == 'O') {
            switch (readByte(50)) {
                case 'A': return ACT_UP;
                case 'B': return ACT_DOWN;
                case 'C': return ACT_RIGHT;
                case 'D': return ACT_LEFT;
            }
        }
        return ACT_NONE;
    }

    // Русская буква в UTF-8 занимает два байта: 0xD0 или 0xD1 и ещё один
    if (c == 0xD0 || c == 0xD1) {
        int c2 = readByte(50);
        if (c2 < 0) return ACT_NONE;
        int letter = ((c & 0x1F) << 6) | (c2 & 0x3F);  // код символа Unicode
        switch (letter) {
            case u'ц': case u'Ц': return ACT_UP;
            case u'ы': case u'Ы': return ACT_DOWN;
            case u'ф': case u'Ф': return ACT_LEFT;
            case u'в': case u'В': return ACT_RIGHT;
            case u'р': case u'Р': return ACT_HELP;
            case u'й': case u'Й': return ACT_QUIT;
        }
        return ACT_NONE;
    }

    switch (c) {
        case 'w': case 'W': return ACT_UP;
        case 's': case 'S': return ACT_DOWN;
        case 'a': case 'A': return ACT_LEFT;
        case 'd': case 'D': return ACT_RIGHT;
        case 'h': case 'H': return ACT_HELP;
        case 'q': case 'Q': return ACT_QUIT;
    }
    return ACT_NONE;
}

// Форма слова «ход» для числа n: 1 ход, 2 хода, 5 ходов
string movesWord(int n) {
    int last2 = n % 100, last = n % 10;
    if (last2 >= 11 && last2 <= 14) return "ходов";
    if (last == 1) return "ход";
    if (last >= 2 && last <= 4) return "хода";
    return "ходов";
}

// ---------------------------------------------------------------------
//  Игровая логика
// ---------------------------------------------------------------------

// Проверяет, занята ли клетка (x, y) одним из первых count сегментов змейки
bool isSnakeCell(const Game& g, int x, int y, int count) {
    for (int i = 0; i < count; i++)
        if (g.x[i] == x && g.y[i] == y) return true;
    return false;
}

// Возвращает номер фрукта, лежащего в клетке (x, y), или -1
int fruitAt(const Game& g, int x, int y) {
    for (int i = 0; i < MAX_FRUITS; i++)
        if (g.fruits[i].active && g.fruits[i].x == x && g.fruits[i].y == y) return i;
    return -1;
}

// Выбирает случайную клетку без змейки и без фруктов. Если maxDist >= 0,
// клетка должна быть не дальше maxDist шагов от головы (по горизонтали + вертикали).
// Возвращает false, если подходящих клеток нет.
bool randomFreeCell(const Game& g, int maxDist, int& outX, int& outY) {
    int freeX[MAX_LEN], freeY[MAX_LEN];  // список подходящих клеток
    int count = 0;
    for (int row = 0; row < HEIGHT; row++)
        for (int col = 0; col < WIDTH; col++) {
            if (isSnakeCell(g, col, row, g.length) || fruitAt(g, col, row) >= 0) continue;
            int dist = abs(col - g.x[0]) + abs(row - g.y[0]);
            if (maxDist >= 0 && dist > maxDist) continue;
            freeX[count] = col;
            freeY[count] = row;
            count++;
        }
    if (count == 0) return false;

    int k = rand() % count;
    outX = freeX[k];
    outY = freeY[k];
    return true;
}

// Кладёт новые яблоки/бананы на пустующие места, чтобы на поле их всегда было REGULAR_FRUITS
void refillRegularFruits(Game& g) {
    for (int i = 0; i < REGULAR_FRUITS; i++) {
        Fruit& f = g.fruits[i];
        if (f.active) continue;
        if (!randomFreeCell(g, -1, f.x, f.y)) return;  // поле заполнено
        f.type   = (rand() % 100 < BANANA_CHANCE) ? BANANA : APPLE;
        f.ttl    = 0;
        f.active = true;
    }
}

// Пробует вырастить вишню рядом с головой. Возвращает true, если вишня появилась.
bool spawnCherry(Game& g) {
    Fruit& c = g.fruits[CHERRY_SLOT];
    if (!randomFreeCell(g, CHERRY_RADIUS, c.x, c.y)) return false;
    c.type   = CHERRY;
    c.ttl    = CHERRY_LIFETIME;
    c.active = true;
    return true;
}

// Начальное состояние: змейка длины 1 в центре поля, фрукты в случайных местах
void initGame(Game& g) {
    g.length = 1;
    g.x[0]   = WIDTH / 2;
    g.y[0]   = HEIGHT / 2;
    g.dir    = ACT_NONE;
    g.score  = 0;
    g.moves  = 0;
    g.cherriesMissed = 0;
    g.result = RES_PLAYING;
    for (int i = 0; i < FRUIT_TYPES; i++) g.eaten[i] = 0;
    for (int i = 0; i < MAX_FRUITS; i++) g.fruits[i].active = false;
    refillRegularFruits(g);
}

// Делает один ход в направлении dir. Возвращает сообщение для игрока.
string makeMove(Game& g, Action dir) {
    // Развернуться на 180° нельзя: голова сразу въехала бы в шею.
    // Такое нажатие не считается ходом, и таймер вишни не идёт.
    if (g.length > 1 && dir == OPPOSITE[g.dir])
        return "Нельзя развернуться назад!";

    bool cherryWasActive = g.fruits[CHERRY_SLOT].active;
    g.dir = dir;
    g.moves++;
    int nx = g.x[0] + DX[dir];
    int ny = g.y[0] + DY[dir];

    // Столкновение со стеной
    if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT) {
        g.result = RES_WALL;
        return "Бум!";
    }

    // Столкновение с хвостом. Если змейка не растёт, кончик хвоста в этот ход
    // освобождает свою клетку, поэтому его не проверяем.
    int  eatenSlot = fruitAt(g, nx, ny);
    bool ate       = (eatenSlot >= 0);
    if (isSnakeCell(g, nx, ny, ate ? g.length : g.length - 1)) {
        g.result = RES_TAIL;
        return "Бум!";
    }

    // Сдвигаем тело: каждый сегмент встаёт на место предыдущего.
    // Если фрукт съеден, длина растёт и последний сегмент остаётся на месте.
    if (ate) g.length++;
    for (int i = g.length - 1; i > 0; i--) {
        g.x[i] = g.x[i - 1];
        g.y[i] = g.y[i - 1];
    }
    g.x[0] = nx;
    g.y[0] = ny;

    string message;
    if (ate) {
        Fruit& f = g.fruits[eatenSlot];
        g.score += FRUIT_POINTS[f.type];
        g.eaten[f.type]++;
        f.active = false;
        message  = string("Ням! ") + FRUIT_NAME[f.type] + " +" + to_string(FRUIT_POINTS[f.type]) + ". ";
    }

    // Вишня: несъеденная «стареет» на один ход и через CHERRY_LIFETIME ходов исчезает.
    // Новая может вырасти только на следующем ходу после того, как прежней не стало.
    Fruit& cherry = g.fruits[CHERRY_SLOT];
    if (cherry.active) {
        cherry.ttl--;
        if (cherry.ttl == 0) {
            cherry.active = false;
            g.cherriesMissed++;
            message += "Вишня исчезла... ";
        }
    } else if (!cherryWasActive && rand() % 100 < CHERRY_CHANCE) {
        if (spawnCherry(g)) message += "Выросла вишня! Успейте за " + to_string(CHERRY_LIFETIME) + " " +
                                       movesWord(CHERRY_LIFETIME) + ". ";
    }

    refillRegularFruits(g);  // на место съеденного яблока или банана — новый фрукт

    if (g.score >= TARGET_SCORE) {
        g.result = RES_WIN;
        message += "Урожай собран!";
    }
    return message;
}

// ---------------------------------------------------------------------
//  Вывод на экран
// ---------------------------------------------------------------------

// Раскладывает фрукты и змейку по двумерному массиву клеток
void buildField(const Game& g, Cell field[HEIGHT][WIDTH]) {
    for (int row = 0; row < HEIGHT; row++)
        for (int col = 0; col < WIDTH; col++)
            field[row][col] = CELL_EMPTY;

    for (int i = 0; i < MAX_FRUITS; i++) {
        const Fruit& f = g.fruits[i];
        if (f.active) field[f.y][f.x] = static_cast<Cell>(CELL_APPLE + f.type);
    }
    for (int i = g.length - 1; i >= 0; i--)
        field[g.y[i]][g.x[i]] = (i == 0) ? CELL_HEAD : CELL_BODY;
}

// Строка стены: "# # # ... #"
string wallLine() {
    string line = "#";
    for (int i = 0; i <= WIDTH; i++) line += " #";
    return paint(line, WALL_COLOR) + "\n";
}

// Рисует поле, счёт, легенду фруктов и подсказку по клавишам
void drawGame(const Game& g, const string& message) {
    Cell field[HEIGHT][WIDTH];
    buildField(g, field);

    string out = clearScreen();
    out += "  САД   Очки: " + to_string(g.score) + "/" + to_string(TARGET_SCORE) +
           "   Длина: " + to_string(g.length) + "   Ход: " + to_string(g.moves) + "\n";
    out += wallLine();
    for (int row = 0; row < HEIGHT; row++) {
        out += paint("#", WALL_COLOR);
        for (int col = 0; col < WIDTH; col++) {
            Cell c = field[row][col];
            out += " " + paint(CELL_SYMBOL[c], CELL_COLOR[c]);
        }
        out += " " + paint("#", WALL_COLOR) + "\n";
    }
    out += wallLine();

    // Легенда: символ, название и цена каждого фрукта
    out += " ";
    for (int t = 0; t < FRUIT_TYPES; t++)
        out += " " + paint(CELL_SYMBOL[CELL_APPLE + t], CELL_COLOR[CELL_APPLE + t]) + " " +
               FRUIT_NAME[t] + " +" + to_string(FRUIT_POINTS[t]) + " ";
    out += "\n";

    const Fruit& cherry = g.fruits[CHERRY_SLOT];
    if (cherry.active)
        out += "  " + paint("Вишня исчезнет через " + to_string(cherry.ttl) + " " + movesWord(cherry.ttl) + "!",
                            CELL_COLOR[CELL_CHERRY]) + "\n";
    else
        out += "\n";

    out += "  " + message + "\n";
    out += "  WASD / стрелки — ход,  H — справка,  Q — выход\n";
    cout << out << flush;
}

// Справка: цель игры, правила и клавиши управления.
// Перед началом игры выводится сама, во время игры — по клавише H.
void showHelp(bool beforeStart) {
    string out = clearScreen();
    out += paint("============= ЗМЕЙКА В САДУ: СПРАВКА =============", WALL_COLOR) + "\n\n";
    out += "Цель игры: собрать урожай — набрать " + to_string(TARGET_SCORE) + " очков.\n\n";
    out += "Фрукты:\n";
    out += "  " + paint("A", CELL_COLOR[CELL_APPLE])  + " — яблоко: +1 очко;\n";
    out += "  " + paint("B", CELL_COLOR[CELL_BANANA]) + " — банан:  +2 очка;\n";
    out += "  " + paint("C", CELL_COLOR[CELL_CHERRY]) + " — вишня:  +3 очка, появляется рядом с головой\n";
    out += "      и исчезает через " + to_string(CHERRY_LIFETIME) + " " + movesWord(CHERRY_LIFETIME) +
           ", если её не съесть.\n\n";
    out += "Правила:\n";
    out += "  - поле " + to_string(WIDTH) + " x " + to_string(HEIGHT) +
           " клеток, змейка (@) начинает в центре, её длина 1;\n";
    out += "  - на поле всегда лежат " + to_string(REGULAR_FRUITS) + " фрукта (яблоки или бананы),\n";
    out += "    вишня вырастает время от времени;\n";
    out += "  - любой съеденный фрукт удлиняет змейку на 1;\n";
    out += "  - каждое нажатие клавиши направления — один ход змейки;\n";
    out += "  - развернуться назад (на 180 градусов) нельзя;\n";
    out += "  - врезаться в стену (#) или в свой хвост (o) — проигрыш.\n\n";
    out += "Управление:\n";
    out += "  W или стрелка вверх   — вверх      S или стрелка вниз  — вниз\n";
    out += "  A или стрелка влево   — влево      D или стрелка вправо — вправо\n";
    out += "  H — эта справка (можно вызвать в любой момент игры)\n";
    out += "  Q — выйти из игры\n";
    out += "  (в русской раскладке работают те же клавиши: Ц Ф Ы В, Р, Й)\n";
    out += paint("==================================================", WALL_COLOR) + "\n\n";
    out += beforeStart ? "Нажмите любую клавишу, чтобы начать игру (Q — выход)...\n"
                       : "Нажмите любую клавишу, чтобы вернуться к игре...\n";
    cout << out << flush;
}

// Итог игры: причина окончания, счёт и статистика по фруктам
void showGameOver(const Game& g) {
    string title;
    const char* color = LOSE_COLOR;
    switch (g.result) {
        case RES_WALL: title = "ИГРА ОКОНЧЕНА: змейка врезалась в стену"; break;
        case RES_TAIL: title = "ИГРА ОКОНЧЕНА: змейка врезалась в свой хвост"; break;
        case RES_WIN:  title = "ПОБЕДА! Урожай собран"; color = WIN_COLOR; break;
        default:       title = "Игра прервана игроком"; break;
    }
    string line(52, '=');
    string out = "\n" + line + "\n";
    out += "  " + paint(title, color) + "\n";
    out += "  Итоговый счёт: " + to_string(g.score) + " из " + to_string(TARGET_SCORE) + "\n";
    out += "  Длина змейки: " + to_string(g.length) + ",  сделано ходов: " + to_string(g.moves) + "\n";
    out += "  Съедено:";
    for (int t = 0; t < FRUIT_TYPES; t++)
        out += string(t ? "," : "") + " " + FRUIT_NAME_GEN[t] + " — " + to_string(g.eaten[t]);
    out += "\n  Упущено вишен: " + to_string(g.cherriesMissed) + "\n";
    out += line + "\n";
    cout << out << flush;
}

// ---------------------------------------------------------------------
//  Главная функция
// ---------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Начальное значение генератора случайных чисел; --seed N позволяет повторить партию
    unsigned seed = static_cast<unsigned>(time(nullptr));
    if (argc == 3 && string(argv[1]) == "--seed")
        seed = static_cast<unsigned>(strtoul(argv[2], nullptr, 10));
    srand(seed);

    setupTerminal();

    // Перед началом игры — правила
    showHelp(true);
    if (readAction() == ACT_QUIT) {
        restoreTerminal();
        cout << "До встречи!\n";
        return 0;
    }

    Game game;
    initGame(game);
    drawGame(game, "Соберите урожай. Удачи!");

    // Главный цикл: одно нажатие клавиши направления — один ход
    while (game.result == RES_PLAYING) {
        Action act = readAction();
        if (act == ACT_NONE) continue;  // клавиша без назначения

        if (act == ACT_QUIT) {
            game.result = RES_QUIT;
            break;
        }
        if (act == ACT_HELP) {
            showHelp(false);
            readAction();               // ждём любую клавишу
            drawGame(game, "");
            continue;
        }
        string message = makeMove(game, act);
        drawGame(game, message);
    }

    showGameOver(game);
    restoreTerminal();
    return 0;
}
