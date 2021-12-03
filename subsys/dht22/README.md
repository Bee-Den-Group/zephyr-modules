# Reading data from DHT22 sensor


    dht22 {
        compatible = "aosong,dht";
        status = "okay";
        label = "DHT22";
        dio-gpios = <&gpio0 22 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
        dht22;
    };