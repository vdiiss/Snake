// =====================================================================
//  Домашнее задание 1. «Змейка на C++»
//  Часть I. Базовая программа
//
//  Змейка ползает по полю 10x10, ест еду, растёт и проигрывает при
//  столкновении со стеной или с собственным хвостом.
//  Одно нажатие клавиши направления — один ход змейки.
//
//  Сборка (VS Code + WSL): g++ -std=c++17 -Wall -Wextra -o snake base/snake.cpp
//  Запуск:                 ./snake            (или ./snake --seed 42)
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
const int WIDTH   = 10;              // ширина поля, клеток
const int HEIGHT  = 10;              // высота поля, клеток
const int MAX_LEN = WIDTH * HEIGHT;  // длиннее числа клеток змейка быть не может

// Действия игрока. Первые пять значений — направления движения,
// поэтому они служат индексами в массивах DX, DY и OPPOSITE.
enum Action { ACT_NONE, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_HELP, ACT_QUIT };

const int    DX[]       = {0,  0, 0, -1, 1};  // смещение головы по X
const int    DY[]       = {0, -1, 1,  0, 0};  // смещение головы по Y (ось направлена вниз)
const Action OPPOSITE[] = {ACT_NONE, ACT_DOWN, ACT_UP, ACT_RIGHT, ACT_LEFT};

// Содержимое клетки поля и его вид на экране (индекс — значение Cell)
enum Cell { CELL_EMPTY, CELL_HEAD, CELL_BODY, CELL_FOOD };
const char* const CELL_SYMBOL[] = {".", "@", "o", "*"};
const char* const CELL_COLOR[]  = {"\033[90m", "\033[1;32m", "\033[32m", "\033[1;31m"};

const char* const WALL_COLOR  = "\033[33m";
const char* const WIN_COLOR   = "\033[1;32m";
const char* const LOSE_COLOR  = "\033[1;31m";
const char* const RESET_COLOR = "\033[0m";

// Чем закончилась игра
enum Result { RES_PLAYING, RES_WALL, RES_TAIL, RES_WIN, RES_QUIT };

// Состояние игры
struct Game {
    int    x[MAX_LEN];    // координаты сегментов змейки; x[0], y[0] — голова
    int    y[MAX_LEN];
    int    length;        // текущая длина змейки
    Action dir;           // направление последнего хода
    int    foodX, foodY;  // где лежит еда
    int    score;         // счёт
    int    moves;         // сколько ходов сделано
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

// ---------------------------------------------------------------------
//  Игровая логика
// ---------------------------------------------------------------------

// Проверяет, занята ли клетка (x, y) одним из первых count сегментов змейки
bool isSnakeCell(const Game& g, int x, int y, int count) {
    for (int i = 0; i < count; i++)
        if (g.x[i] == x && g.y[i] == y) return true;
    return false;
}

// Кладёт еду в случайную свободную клетку.
// Возвращает false, если свободных клеток не осталось.
bool placeFood(Game& g) {
    int freeX[MAX_LEN], freeY[MAX_LEN];  // список свободных клеток
    int count = 0;
    for (int row = 0; row < HEIGHT; row++)
        for (int col = 0; col < WIDTH; col++)
            if (!isSnakeCell(g, col, row, g.length)) {
                freeX[count] = col;
                freeY[count] = row;
                count++;
            }
    if (count == 0) return false;

    int k = rand() % count;
    g.foodX = freeX[k];
    g.foodY = freeY[k];
    return true;
}

// Начальное состояние: змейка длины 1 в центре поля, еда в случайном месте
void initGame(Game& g) {
    g.length = 1;
    g.x[0]   = WIDTH / 2;
    g.y[0]   = HEIGHT / 2;
    g.dir    = ACT_NONE;
    g.score  = 0;
    g.moves  = 0;
    g.result = RES_PLAYING;
    placeFood(g);
}

// Делает один ход в направлении dir. Возвращает сообщение для игрока.
string makeMove(Game& g, Action dir) {
    // Развернуться на 180° нельзя: голова сразу въехала бы в шею
    if (g.length > 1 && dir == OPPOSITE[g.dir])
        return "Нельзя развернуться назад!";

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
    bool ate = (nx == g.foodX && ny == g.foodY);
    if (isSnakeCell(g, nx, ny, ate ? g.length : g.length - 1)) {
        g.result = RES_TAIL;
        return "Бум!";
    }

    // Сдвигаем тело: каждый сегмент встаёт на место предыдущего.
    // Если еда съедена, длина растёт и последний сегмент остаётся на месте.
    if (ate) g.length++;
    for (int i = g.length - 1; i > 0; i--) {
        g.x[i] = g.x[i - 1];
        g.y[i] = g.y[i - 1];
    }
    g.x[0] = nx;
    g.y[0] = ny;

    if (!ate) return "";

    g.score++;
    if (!placeFood(g)) {  // еду некуда положить — змейка заняла всё поле
        g.result = RES_WIN;
        return "Поле заполнено!";
    }
    return "Ням! +1 очко";
}

// ---------------------------------------------------------------------
//  Вывод на экран
// ---------------------------------------------------------------------

// Раскладывает еду и змейку по двумерному массиву клеток
void buildField(const Game& g, Cell field[HEIGHT][WIDTH]) {
    for (int row = 0; row < HEIGHT; row++)
        for (int col = 0; col < WIDTH; col++)
            field[row][col] = CELL_EMPTY;

    field[g.foodY][g.foodX] = CELL_FOOD;
    for (int i = g.length - 1; i >= 0; i--)
        field[g.y[i]][g.x[i]] = (i == 0) ? CELL_HEAD : CELL_BODY;
}

// Строка стены: "# # # ... #"
string wallLine() {
    string line = "#";
    for (int i = 0; i <= WIDTH; i++) line += " #";
    return paint(line, WALL_COLOR) + "\n";
}

// Рисует поле, счёт и подсказку по клавишам
void drawGame(const Game& g, const string& message) {
    Cell field[HEIGHT][WIDTH];
    buildField(g, field);

    string out = clearScreen();
    out += "  ЗМЕЙКА   Счёт: " + to_string(g.score) + "   Длина: " + to_string(g.length) +
           "   Ход: " + to_string(g.moves) + "\n";
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
    out += "  " + message + "\n";
    out += "  WASD / стрелки — ход,  H — справка,  Q — выход\n";
    cout << out << flush;
}

// Справка: цель игры, правила и клавиши управления.
// Перед началом игры выводится сама, во время игры — по клавише H.
void showHelp(bool beforeStart) {
    string out = clearScreen();
    out += paint("================ ЗМЕЙКА: СПРАВКА ================", WALL_COLOR) + "\n\n";
    out += "Цель игры: съесть как можно больше еды и вырасти как можно длиннее.\n\n";
    out += "Правила:\n";
    out += "  - поле " + to_string(WIDTH) + " x " + to_string(HEIGHT) +
           " клеток, змейка (@) начинает в центре, её длина 1;\n";
    out += "  - еда (*) появляется в случайной свободной клетке;\n";
    out += "  - съев еду, змейка становится длиннее на 1, счёт растёт на 1;\n";
    out += "  - каждое нажатие клавиши направления — один ход змейки;\n";
    out += "  - развернуться назад (на 180 градусов) нельзя;\n";
    out += "  - врезаться в стену (#) или в свой хвост (o) — проигрыш;\n";
    out += "  - змейка заняла всё поле — победа!\n\n";
    out += "Управление:\n";
    out += "  W или стрелка вверх   — вверх      S или стрелка вниз  — вниз\n";
    out += "  A или стрелка влево   — влево      D или стрелка вправо — вправо\n";
    out += "  H — эта справка (можно вызвать в любой момент игры)\n";
    out += "  Q — выйти из игры\n";
    out += "  (в русской раскладке работают те же клавиши: Ц Ф Ы В, Р, Й)\n";
    out += paint("=================================================", WALL_COLOR) + "\n\n";
    out += beforeStart ? "Нажмите любую клавишу, чтобы начать игру (Q — выход)...\n"
                       : "Нажмите любую клавишу, чтобы вернуться к игре...\n";
    cout << out << flush;
}

// Итог игры: причина окончания и счёт
void showGameOver(const Game& g) {
    string title;
    const char* color = LOSE_COLOR;
    switch (g.result) {
        case RES_WALL: title = "ИГРА ОКОНЧЕНА: змейка врезалась в стену"; break;
        case RES_TAIL: title = "ИГРА ОКОНЧЕНА: змейка врезалась в свой хвост"; break;
        case RES_WIN:  title = "ПОБЕДА! Змейка заняла всё поле"; color = WIN_COLOR; break;
        default:       title = "Игра прервана игроком"; break;
    }
    string line(48, '=');
    string out = "\n" + line + "\n";
    out += "  " + paint(title, color) + "\n";
    out += "  Итоговый счёт: " + to_string(g.score) + "\n";
    out += "  Длина змейки: " + to_string(g.length) + ",  сделано ходов: " + to_string(g.moves) + "\n";
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
    drawGame(game, "Съешьте еду (*). Удачи!");

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
