import nox


@nox.session(python=["3.5", "3.6", "3.7", "3.8", "3.9"])
def test(session):
    session.install("pytest", ".")
    session.run(
        "pytest", "-ra", "--tb=native", "test-noaho.py",
    )


@nox.session
def lint(session):
    session.install("flake8")
    session.run("flake8", "--version")
    session.run("flake8", "src", "tests", "noxfile.py", "setup.py")
