import sys

from setuptools import setup, Extension

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

version = "0.10.0"

setup(
    name="noahong",
    version=version,
    author="Jeff Donner",
    author_email="jeffrey.donner@gmail.com",
    url="https://github.com/clustree/noahong",
    description="Fast, non-overlapping simultaneous multiple keyword search",
    long_description=open("README.txt").read(),
    long_description_content_type="text/plain",
    ext_modules=[noaho_module],
    classifiers=[
        "Intended Audience :: Developers",
        "Intended Audience :: Information Technology",
        "License :: OSI Approved :: MIT License",
        "Development Status :: 4 - Beta",
        "Programming Language :: C++",
        "Programming Language :: Cython",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Operating System :: OS Independent",
        "Environment :: Console",
        "Topic :: Text Processing",
    ],
    python_requires=">=3.6, <4",
)
