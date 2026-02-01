import matplotlib.pyplot as plt
data = open("log.txt").read().strip().split("\n")
x: list[int] = [int(x.split(",")[0]) for x in data]
y = [int(x.split(",")[1]) for x in data]
plt.plot(x, y)
plt.xlabel("recursion depth")
plt.ylabel("Time (cycles)")
plt.show()

