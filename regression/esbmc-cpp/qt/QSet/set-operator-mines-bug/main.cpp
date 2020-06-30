// constructing QSets
#include <iostream>
#include <QSet>
#include <cassert>
using namespace std;

int main ()
{
    QSet<int> first;
    first.insert(1);
    first.insert(2);
    QSet<int> second;
    second.insert(1);
    second.insert(2);
    second.insert(3);
    QSet<int> third;
    third = first - second;
    assert(third.size() != 1);
    return 0;
}
