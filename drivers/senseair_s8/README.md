# Driver for Sensair S8

    &uart1 {
        status = "okay";
        tx-pin = <5>;
        rx-pin = <4>;
        current-speed = <9600>;

        s8modbus: modbus0 {
            compatible = "zephyr,modbus-serial";
            status = "okay";
            label = "MODBUS0";
        };
    };