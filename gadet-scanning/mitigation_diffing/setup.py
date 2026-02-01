from setuptools import setup, find_packages

setup(
    name="mitigation-diffing",
    version="1.0.0",
    packages=find_packages(),
    install_requires=["transformers", "torch", "numpy"],
    entry_points={"console_scripts": ["mitigation-diffing=mitigation_diffing.cli:main"]},
)
