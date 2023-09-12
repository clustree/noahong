import nox


@nox.session(python=["3.7", "3.8", "3.9", "3.10", "3.11"])
def test(session):
    session.install("pytest", ".")
    session.run("pytest", "-ra", "--tb=native", "--verbose", "test-noaho.py")


@nox.session
def lint(session):
    session.install("flake8", "black")
    session.run("flake8", "--version")
    session.run("black", "--version")
    session.run("black", "--check", "src", "tests", "noxfile.py", "setup.py")
    session.run("flake8", "src", "tests", "noxfile.py", "setup.py")


@nox.session
def blacken(session):
    """Run black code formatter."""
    session.install("black")
    session.run("black", "src", "tests", "noxfile.py", "setup.py")

    lint(session)
