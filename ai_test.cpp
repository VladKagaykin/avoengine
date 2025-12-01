#include <iostream>
#include <ctime>

int main() {
    clock_t start = clock(); // Время начала
    while(true){
        clock_t end = clock();
        double elapsed_time = static_cast<double>(end - start) / CLOCKS_PER_SEC;
        //std::cout <<int(elapsed_time)<< std::endl;
    }
    return 0;
}
