pinmode('D4','output',1);
delay(1000);
for k=1:5
    disp(k)
    digitalwrite('D4',0);
    delay(500);
    digitalwrite('D4',1);
    delay(500);
end
digitalread('D4')
