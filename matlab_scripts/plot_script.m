clc;close all;clear all;

fid = fopen('../result_template.txt');

tline = fgetl(fid);
key = 'Critial_read_Latency';
index = strfind(tline, key);
while(ischar(tline))
    
    disp(tline);
    tline = fgetl(fid);
    
    
    tline = tline(index+length(key):end);
    
end