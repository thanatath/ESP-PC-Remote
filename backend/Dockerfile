# Use an official Bun image as a parent image
FROM oven/bun:latest

# Set the working directory in the container
WORKDIR /usr/src/app

# Copy the application file into the container
COPY main.js .

# Make port 3000 available to the world outside this container
EXPOSE 3000

# Run main.js when the container launches
CMD ["bun", "run", "main.js"]
