import sys

from distutils.core import setup
from distutils.extension import Extension

extra_args = ["-std=c++11"]
if sys.platform == "darwin":
    extra_args.append("-stdlib=libc++")

noaho_module = Extension(
    "noahong",
    sources=[
        # Cython generated
        "src/noahong.cpp",
        # original
        "src/array-aho.cpp",
    ],
    depends=["src/array-aho.h", "src/noahong.pyx"],
    extra_compile_args=extra_args,
    extra_link_args=extra_args,
)

version = "0.9.7"

setup(
    name="noahong",
    version=version,
    author="Jeff Donner",
    author_email="jeffrey.donner@gmail.com",
    maintainer="Jeff Donner",
    maintainer_email="jeffrey.donner@gmail.com",
    url="https://github.com/JDonner/NoAho",
    description="Fast, non-overlapping simultaneous multiple keyword search",
    long_description=open("README.txt").read(),
    ext_modules=[noaho_module],
    classifiers=[
        "Intended Audience :: Developers",
        "Intended Audience :: Information Technology",
        "License :: OSI Approved :: MIT License",
        "Development Status :: 4 - Beta",
        "Programming Language :: C++",
        "Programming Language :: Cython",
        "Programming Language :: Python :: 2",
        "Programming Language :: Python :: 2.7",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.0",
        "Programming Language :: Python :: 3.1",
        "Programming Language :: Python :: 3.2",
        "Programming Language :: Python :: 3.3",
        "Programming Language :: Python :: 3.4",
        "Programming Language :: Python :: 3.5",
        "Operating System :: OS Independent",
        "Environment :: Console",
        "Topic :: Text Processing",
    ],
)
