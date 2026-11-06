// Prints every model's control captions at 0, 25, 50, 75 and 100 percent of
// their travel, as the screen shows them, for the README's table. Not part of
// the machine.
#include "../display.cpp"
#include <cstdio>

int main() {
    static const int at[5] = {0, 32, 64, 95, 127};
    for (unsigned m = 0; m < felt::model_count; ++m)
        for (unsigned s = 0; s < 8; ++s) {
            std::printf("%s %s", felt::view::names[m], felt::view::labels[m][s]);
            for (int k = 0; k < 5; ++k) {
                char text[felt::view::cap];
                felt::view::caption(text, felt::view::captions[m][s], (at[k] * 8257) >> 10, 0);
                std::printf(" %s", text);
            }
            std::printf("\n");
        }
    return 0;
}
