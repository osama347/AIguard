#include "test.h"

std::vector<test::Case>& test::registry() {
    static std::vector<Case> cases;
    return cases;
}

int main() {
    int failed = 0;
    for (const auto& c : test::registry()) {
        try {
            c.fn();
            std::cout << "  ok    " << c.name << "\n";
        } catch (const test::Failure& f) {
            ++failed;
            std::cout << "  FAIL  " << c.name << "\n        " << f.msg << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "  FAIL  " << c.name << "\n        exception: " << e.what() << "\n";
        }
    }
    std::cout << (test::registry().size() - failed) << "/" << test::registry().size() << " passed\n";
    return failed ? 1 : 0;
}
