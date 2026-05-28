#include <vector>
#include <iostream>
#include <string>

// Correct way to write a template function
template<class T, std::size_t N>
void do_something() {
    std::vector<T> v;
    T x = 'a'; // Default initialization

    for (std::size_t i = 0; i < N; ++i) {
        v.push_back(x);
        std::cout << "Pushed: " << v.back() << "\n";
    }

}

int main() {
    // Call the template function with specific types and sizes
    // fixed_vector<char, 10'000> fv; // Removed undefined identifier
    do_something<char, 10'000>();
    return 0;
}