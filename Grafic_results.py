# заменить res.txt своим файлом с результатами и запустить файл

import matplotlib.pyplot as plt
import matplotlib.dates as md
import pandas as pd
import datetime as dt

data = pd.read_csv('res.txt', sep='\s+', header = None)
data = pd.DataFrame(data)
timestamps = data[1] + 1700000000
dates = [dt.datetime.fromtimestamp(ts) for ts in timestamps]
datenums = md.date2num(dates)
values = data[0]
plt.subplots_adjust(bottom=0.2)
plt.xticks( rotation=25 )
ax=plt.gca()
xfmt = md.DateFormatter('%m.%d %H:%M')
ax.xaxis.set_major_formatter(xfmt)
plt.scatter(datenums,values)
plt.plot(datenums,values)
plt.minorticks_on()
plt.grid(which='major', color = '#444', linewidth = 0.4)
plt.grid(which='minor', ls=':')
plt.xlabel("Дата и время", fontsize=18)
plt.ylabel("Вес, кг", fontsize=18)
plt.show()