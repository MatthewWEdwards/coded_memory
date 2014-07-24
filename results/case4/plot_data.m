clc;close all;clear all;

suffix_name = 'Case4';
load coding_results
load baseline_results

% Plot Critical Read Latency
h = figure;
semilogy(baseline_results(:,1),baseline_results(:,2),'--');
hold on;
semilogy(coding_results(:,1),coding_results(:,2),'-*');
legend('trace1Baseline','trace1Coding');
% 'trace2Baseline','trace3Baseline','trace4Baseline','trace1Coding','trace2Coding','trace3Coding','trace4Coding')
xlabel('Access Ratio in ns');
ylabel('Latency in ns')
title(['Critical Read Latency for ' suffix_name]);
saveas(h,'Critical_Read_Latency.jpg');


% Plot Transactional Read Latency
h = figure;
semilogy(baseline_results(:,1),baseline_results(:,3),'--');
hold on;
semilogy(coding_results(:,1),coding_results(:,3),'-*');
legend('trace1Baseline','trace1Coding');
% 'trace2Baseline','trace3Baseline','trace4Baseline','trace1Coding','trace2Coding','trace3Coding','trace4Coding')
xlabel('Access Ratio in ns');
ylabel('Latency in ns')
title(['Transactional Read Latency for ' suffix_name]);
saveas(h,'Transactional_Read_Latency.jpg');


% Plot Write Latency
h = figure;
semilogy(baseline_results(:,1),baseline_results(:,4),'--');
hold on;
semilogy(coding_results(:,1),coding_results(:,4),'-*');
legend('trace1Baseline','trace1Coding');
% 'trace2Baseline','trace3Baseline','trace4Baseline','trace1Coding','trace2Coding','trace3Coding','trace4Coding')
xlabel('Access Ratio in ns');
ylabel('Latency in ns')
title(['Write Latency for ' suffix_name]);
saveas(h,'Write_Latency.jpg');

% Plot Trace Execution Time in ns
h = figure;
semilogy(baseline_results(:,1),baseline_results(:,3),'--');
hold on;
semilogy(coding_results(:,1),coding_results(:,3),'-*');
legend('trace1Baseline','trace1Coding');
% 'trace2Baseline','trace3Baseline','trace4Baseline','trace1Coding','trace2Coding','trace3Coding','trace4Coding')
xlabel('Access Ratio in ns');
ylabel('Execution time in ns')
title(['Access time for ' suffix_name]);
saveas(h,'Access_time.jpg');