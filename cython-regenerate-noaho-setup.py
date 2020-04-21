from setuptools import setup, Extension
import sys

from Cython.Distutils import build_ext

# run with:
# python cython-regenerate-noaho-setup.py build_ext --inplace

extra_args = ["-std=c++11"]
if sys.platform == "darwin":
    extra_args.append("-stdlib=libc++")

setup(
    ext_modules=[
        Extension(
            "noahong",
            ["src/noahong.pyx", "src/array-aho.cpp"],
            language="c++",
            extra_compile_args=extra_args,
            extra_link_args=extra_args,
        )
    ],
    cmdclass={"build_ext": build_ext},
)
