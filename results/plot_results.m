clc;close all;clear all;
Font_Size = 14;



importfile('','baseline_results.txt');
importfile('','write_noreplacement_lookahead_benefits\coding_results_0.txt');
importfile('','write_noreplacement_lookahead_benefits\coding_results_5.txt');
importfile('','write_noreplacement_lookahead_benefits\coding_results_10.txt');
importfile('','write_noreplacement_lookahead_benefits\coding_results_15.txt');
importfile('','write_noreplacement_lookahead_benefits\coding_results_20.txt');

importfile('only_read_','no_write_lookahead_benefits\coding_results_0.txt');
importfile('only_read_','no_write_lookahead_benefits\coding_results_5.txt');
importfile('only_read_','no_write_lookahead_benefits\coding_results_10.txt');
importfile('only_read_','no_write_lookahead_benefits\coding_results_15.txt');
importfile('only_read_','no_write_lookahead_benefits\coding_results_20.txt');


figure; set(gca,'FontSize',Font_Size);
plot(baseline_results(:,1),baseline_results(:,2));hold on;
plot(coding_results_0(:,1),coding_results_0(:,2),'--r','LineWidth',2);hold on;
plot(coding_results_5(:,1),coding_results_5(:,2),'--g','LineWidth',2);hold on;
plot(coding_results_10(:,1),coding_results_10(:,2),'--b','LineWidth',2);hold on;
plot(coding_results_15(:,1),coding_results_15(:,2),'--c','LineWidth',2);hold on;
plot(coding_results_20(:,1),coding_results_20(:,2),'--k','LineWidth',2);hold on;
set(gca,'yscale','log');
xlabel('Access time in ns');
ylabel('Critial Word Read Latency in ns');
legend('Baseline','Coding with no look ahead','Coding with look-ahead(5)','Coding with look-ahead(10)','Coding with look-ahead(15)','Coding with look-ahead(20)');

figure; set(gca,'FontSize',Font_Size);
plot(baseline_results(:,1),baseline_results(:,3));hold on;
plot(coding_results_0(:,1),coding_results_0(:,3),'--r','LineWidth',2);hold on;
plot(coding_results_5(:,1),coding_results_5(:,3),'--g','LineWidth',2);hold on;
plot(coding_results_10(:,1),coding_results_10(:,3),'--b','LineWidth',2);hold on;
plot(coding_results_15(:,1),coding_results_15(:,3),'--c','LineWidth',2);hold on;
plot(coding_results_20(:,1),coding_results_20(:,3),'--y','LineWidth',2);hold on;
set(gca,'yscale','log');
xlabel('Access time in ns');
ylabel('Transactional Read Latency in ns');
legend('Baseline','Coding with no look ahead','Coding with look-ahead(5)','Coding with look-ahead(10)','Coding with look-ahead(15)','Coding with look-ahead(20)');


figure; set(gca,'FontSize',Font_Size);
plot(baseline_results(:,1),baseline_results(:,4));hold on;
plot(coding_results_0(:,1),coding_results_0(:,4),'--r','LineWidth',2);hold on;
plot(coding_results_5(:,1),coding_results_5(:,4),'--g','LineWidth',2);hold on;
plot(coding_results_10(:,1),coding_results_10(:,4),'--b','LineWidth',2);hold on;
plot(coding_results_15(:,1),coding_results_15(:,4),'--c','LineWidth',2);hold on;
plot(coding_results_20(:,1),coding_results_20(:,4),'--y','LineWidth',2);hold on;
set(gca,'yscale','log');
xlabel('Access time in ns');
ylabel('Transactional Write Latency in ns');
legend('Baseline','Coding with no look ahead','Coding with look-ahead(5)','Coding with look-ahead(10)','Coding with look-ahead(15)','Coding with look-ahead(20)');