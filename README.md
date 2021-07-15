Задача следующая:
Нужно написать простой http клиент. По типу curl. Без использования http клиентов из библиотек. Для tcp можно
использовать boost asio например или epoll или что более удобно. SSL не нужен. Достаточно чтобы он умел делать GET и
POST. Хотелось бы иметь поддержку chunked encoding. multipart/form-data не нужен.

Запускаться должен как консольное приложение, вида:
```shell
./mycurl -m POST "userId=1&id=1&title=test&body=test1" jsonplaceholder.typicode.com/posts
```