#include "manipulatedFrame.h"
#include <qapplication.h>

int main(int argc, char** argv)
{
  QApplication application(argc,argv);

  Viewer viewer;

#if QT_VERSION < 0x040000
  application.setMainWidget(&viewer);
#else
  viewer.setWindowTitle("manipulatedFrame");
#endif

  viewer.show();

  return application.exec();
}
