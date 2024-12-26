# заменить result.txt своим файлом с результатами и запустить файл
# наведись курсором на точку графика -- покажет значение

import plotly.graph_objs as go
import pandas as pd
import datetime as dt

data = pd.read_csv('result.txt', sep='\s+', header = None)
data = pd.DataFrame(data)
timestamps = data[1] + 1700000000            # восстановление настоящего времени из сокращенного
dates = [dt.datetime.fromtimestamp(ts) for ts in timestamps] # преобразование unix-времени в формат г.м.д-ч.м.с.

fig = go.Figure()
fig.add_trace(go.Scatter(x=dates, y=data[0], mode='lines+markers'))
fig.update_layout(title="Как со временем менялся вес улья",
                  title_x =0.5,
                  title_font_size=24,
                  xaxis_title="Дата и время",
                  yaxis_title="Вес, кг", autosize=True)
fig.show()

# Можно сразу писать так fig.add_trace(go.Scatter(x=data[1], y=data[0],...), если в файле result.txt время будет в формате г.м.д-ч.м.с
# Это вполне реализуемо, если прописать для сохранения в файл веса-времени время в таком формате
# Однако оптимальнее прибавлять постоянную к срезанному unix-времени из файла.
# За счет отсутствия неизменяющихся постоянных цифр экономится память микроконтроллера, и уменьшается время передачи даннах телеграмм-боту (должно быть :))