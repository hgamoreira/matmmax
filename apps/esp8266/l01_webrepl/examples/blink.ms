pinmode('D4','output',1);
for k=1:5
    digitalwrite('D4',0);
    delay(250);
    digitalwrite('D4',1);
    delay(250);
end
digitalread('D4')
