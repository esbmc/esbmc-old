#include <iostream>
#include <QLinkedList>
#include <QString>
#include <cassert>
using namespace std;

int main ()
{
    QLinkedList<QString> list;
    list << "A" << "B" << "C" << "D" << "E" << "F";
    assert(list.takeLast() == "F");
    assert(list.size() == 5);
  return 0;
}
