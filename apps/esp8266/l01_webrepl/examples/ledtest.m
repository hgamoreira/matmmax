% ledtest: blink the D1 mini's blue LED on D4 five times, then leave it off.
% The LED is active low: writing 0 lights it, 1 turns it off.
pinmode('D4','output',1);
for k=1:5
    fprintf('blink %d of 5\n',k);
    digitalwrite('D4',0);
    delay(300);
    digitalwrite('D4',1);
    delay(300);
end
disp('LED test done; D4 is high (LED off).')
digitalread('D4')
