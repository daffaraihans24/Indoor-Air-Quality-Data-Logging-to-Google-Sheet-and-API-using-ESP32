# Indoor Air Quality using ESP32

Hal pertama yang perlu anda lakukan untuk membuat projek Indoor Air Quality apabila menggunakan MQ135 adalah : 

1. Mengganti nilai resistor (RL) bawaan pada sensor MQ135 dengan resistor sebesar 20kΩ. Untuk lebih jelasnya anda bisa lihat di datasheet : https://www.olimex.com/Products/Components/Sensors/Gas/SNS-MQ135/resources/SNS-MQ135.pdf

2. Mencari nilai a dan b dari grafik pengujian yang terdapat pada datasheet kemudian gunakan power regression, lebih lengkapnya bisa membaca di blog ini : https://davidegironi.blogspot.com/2017/05/mq-gas-sensor-correlation-function.html#.XyxLkIgzbb0

3. Pada program ini menggunakan 2 metode pengiriman data yaitu menggunakan HTTP REST API dan ke Spreadsheet. Anda bisa menggunakan keduanya atau salah satunya.

Berikut wiring diagramnya : 

![WIRING DIAGRAM](https://github.com/user-attachments/assets/345d993f-4a61-4e64-8e7d-f43055ab4034)
