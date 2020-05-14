import matplotlib.pyplot as plt

x = []
y = []

with open("../obj/CWND.csv", "r") as fp:
    fp.readline()
    for line in fp:
        data = line.strip().split(",")
        x.append(float(data[0]))
        y.append(float(data[1]))

plt.plot(x, y)
plt.xlabel("Time (seconds)")
plt.ylabel("Window Size")
plt.title("Congestion Window Over Time")
plt.show()
# plt.savefig("../graphs/cwnd.pdf")

