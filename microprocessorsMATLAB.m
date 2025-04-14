portList = serialportlist("available");
display(portList);

s = serialport(portList(2), 115200);
flush(s); % Clear buffer

% Memory allocation
maxReadings = 256;  
parsedDataList = {}; % Cell array to hold multiple datasets
currentParsedData = NaN(2, maxReadings);  % 2 rows (distance, angle)
count = 0;
dataArrayCount = 0;  % Data array tracker

numRotations = 3; % Now collecting 10 full 360-degree rotations

% Collect all the data before plotting
while dataArrayCount < numRotations  % Stop after 10 data arrays
    if s.NumBytesAvailable > 0  % Check if data is available
        newData = readline(s);
        disp(newData);

        % Check for "360 degrees completed!" condition
        if contains(newData, "360 degrees completed!")
            disp("360 degrees completed. Starting new data collection.");

            % Store the current data array and label it
            dataArrayCount = dataArrayCount + 1;
            parsedDataList{end+1} = currentParsedData(:, ~isnan(currentParsedData(1, :)));
            assignin('base', sprintf('dataArray%d', dataArrayCount), parsedDataList{end});

            % Reinitialize for new dataArray
            currentParsedData = NaN(2, maxReadings);  % Reset for new array
            count = 0; % Reset counter
        end

        % Extract distance and angle (int for distance, double for degrees)
        tokens = regexp(newData, "Distance: (\d+) mm after ([\d.]+) degrees", "tokens");
        
        if ~isempty(tokens)  % If match is found
            count = count + 1; 
            
            % Store values in the current data array
            currentParsedData(1, count) = str2double(tokens{1}{1}); % Distance
            currentParsedData(2, count) = str2double(tokens{1}{2}); % Angle
        end
    else
        pause(0.1); % Avoid MATLAB timeout
    end
end

disp("Data collection complete.");

% Store X, Y, and Z for all datasets
X_all = cell(1, numRotations);
Y_all = cell(1, numRotations);
Z_all = cell(1, numRotations);

% Convert Polar to Cartesian
for i = 1:numRotations
    currentData = parsedDataList{i};
    X = NaN(1, size(currentData, 2));
    Y = NaN(1, size(currentData, 2));
    
    for j = 1:size(currentData, 2)
        distance = currentData(1, j);  % Distance 
        angle = deg2rad(currentData(2, j));  % Angle 

        % Convert to Cartesian coordinates (X, Y)
        X(j) = distance * cos(angle);  
        Y(j) = distance * sin(angle);  
    end
    
    % Set Z values for each array (each rotation is a new Z layer) multiply
    % by number * i - 1 to change depth increase per 360deg
    Z = ones(1, length(X)) * (i - 1);
    
    % Store in lists
    X_all{i} = X;
    Y_all{i} = Y;
    Z_all{i} = Z;
    
    % Assign the converted coordinates to the workspace
    assignin('base', sprintf('newDataArray%d', i), [X; Y; Z]);
end

% Plot the Cartesian coordinates in 3D
figure;
hold on;
xlabel('X');
ylabel('Z'); % Z is horizontal axis
zlabel('Y'); % Y is vertical axis
grid on;
view(3);

colorBlue = [0, 0, 1]; % Define blue color

for i = 1:numRotations
    plot3(X_all{i}, Z_all{i}, Y_all{i}, '-', 'DisplayName', sprintf('newDataArray%d', i), 'LineWidth', 1.5, 'Color', colorBlue);
end

legend show;
title('3D Cartesian Coordinate Plot with 10 Rotations');
hold off;
