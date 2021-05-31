# 解决病理图像文件大于4个G,openslide无法读取的问题，需要直接从github仓库里面拉去进行安装，下面会写清楚安装的步骤
1、安装openslide，将本仓库clone之后
    apt autoconf, automake, libtool, and pkg-config 
    然后进入目录，终端输入命令 "autoreconf -i"
    之后会生成一些有关的文件
    然后直接执行./configure 可能报错openjpeg包没有安装 github去拉去这个包，然后编译安装
    下载压缩包并解压（这里我尝试直接clone，但是编译过程会出问题，后来下载了压缩包就好了） 进入解压后的目录openjpeg-master，依次执行执行如下命令
    mkdir build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    sudo make install
    sudo make clean
    后面可能还会提示sqlite3没有安装，这时候需要 apt-get install libsqlite3-dev，之后在继续执行
    ./configure
    sudo make
    sudo make install
    后面可能还会提示libopenslide没有安装，需要将编译好的文件放到对应的cond环境下面去
    // 进入自己的conda环境，如：~/anaconda3/lib
    cp /usr/local/lib/libopenslide.so.0 ./
    cp /usr/local/lib/libopenslide.so ./
**********************************************************上面就是安装的过程，相关的过程参考如下链接****************************************************
https://blog.csdn.net/chongchong247619000/article/details/103930005

OpenSlide

Carnegie Mellon University and others

https://openslide.org/


==========================


What is this?
=============

This library reads whole slide image files (also known as virtual slides).
It provides a consistent and simple API for reading files from multiple
vendors.


What is the license?
====================

This code is licensed under the GNU LGPL version 2.1, not any later version.
See the file lgpl-2.1.txt for the text of the license.


Requirements
============

This library requires zlib, libpng, libjpeg, libtiff, OpenJPEG 1.x or >= 2.1,
GDK-PixBuf, libxml2, SQLite >= 3.6.20, cairo >= 1.2, and glib >= 2.16.
Leica and Ventana support require libtiff >= 4.

If you want to run the test suite, you will need PyYAML, python-requests,
xdelta3, cjpeg and djpeg (from libjpeg), a Git checkout of OpenSlide,
at least one installed font, and > 120 GB of disk space.  Valgrind mode
requires Valgrind, plus debug symbols for library dependencies (particularly
glib2) and Fontconfig.  Profile mode requires Valgrind.  Coverage mode
requires gcov and Doxygen.


Features
========

The library can read Aperio, Hamamatsu, Leica, MIRAX, Sakura, Trestle,
and Ventana formats, as well as TIFF files that conform to a simple
convention. (InterScope files tend to be readable as this generic TIFF.)

More information about formats is here:
https://openslide.org/formats/

An openslide_t object can be used concurrently from multiple threads
without locking. (But you must lock or otherwise use memory barriers
when passing the object between threads.)


Properties
==========

The library exposes certain properties as string key-value pairs for
a given virtual slide. (These are accessed by way of the
"openslide_get_property_names" and "openslide_get_property_value" calls.)

These properties are generally uninterpreted data gathered from the
on-disk files. New properties can be added over time in subsequent releases
of OpenSlide. A list of some properties can be found at:
https://openslide.org/properties/

OpenSlide itself creates these properties (for now):

 openslide.background-color
   The background color of the slide, given as an RGB hex triplet.
   This property is not always present.

 openslide.bounds-height
   The height of the rectangle bounding the non-empty region of the slide.
   This property is not always present.

 openslide.bounds-width
   The width of the rectangle bounding the non-empty region of the slide.
   This property is not always present.

 openslide.bounds-x
   The X coordinate of the rectangle bounding the non-empty region of the
   slide. This property is not always present.

 openslide.bounds-y
   The Y coordinate of the rectangle bounding the non-empty region of the
   slide. This property is not always present.

 openslide.comment
   A free-form text comment.

 openslide.mpp-x
   Microns per pixel in the X dimension of level 0. May not be present or
   accurate.

 openslide.mpp-y
   Microns per pixel in the Y dimension of level 0. May not be present or
   accurate.

 openslide.objective-power
   Magnification power of the objective. Often inaccurate; sometimes missing.

 openslide.quickhash-1
   A non-cryptographic hash of a subset of the slide data. It can be used
   to uniquely identify a particular virtual slide, but cannot be used
   to detect file corruption or modification.

 openslide.vendor
   The name of the vendor backend.


Other Documentation
===================

The definitive API reference is in openslide.h. For an HTML version, see
doc/html/openslide_8h.html in this distribution.

Additional documentation is available from the OpenSlide website:
https://openslide.org/

The design and implementation of the library are described in a published
technical note:

 OpenSlide: A Vendor-Neutral Software Foundation for Digital Pathology
 Adam Goode, Benjamin Gilbert, Jan Harkes, Drazen Jukic, M. Satyanarayanan
 Journal of Pathology Informatics 2013, 4:27

 http://download.openslide.org/docs/JPatholInform_2013_4_1_27_119005.pdf

There is also an older technical report:

 CMU-CS-08-136
 A Vendor-Neutral Library and Viewer for Whole-Slide Images
 Adam Goode, M. Satyanarayanan

 http://reports-archive.adm.cs.cmu.edu/anon/2008/abstracts/08-136.html
 http://reports-archive.adm.cs.cmu.edu/anon/2008/CMU-CS-08-136.pdf


Acknowledgements
================

OpenSlide has been supported by the National Institutes of Health and
the Clinical and Translational Science Institute at the University of
Pittsburgh.


How to build?
=============

./configure
make
make install

(If building from the Git repository, you will first need to install
autoconf, automake, libtool, and pkg-config and run "autoreconf -i".)

Good luck!
