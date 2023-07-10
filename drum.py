# Goal :
# - Animate a rotating drum with multiple points on the surface of the drum
# - While rotating the drum, display an arrow for the last selected point to the next target point
# - When the next target point is at the start line, set the last selected point to the target, remove the target, calculate the next target
# - Then add a random point
# - Loop this forever

# Python code :
# - Start with a given number of points in the LBA numpy array
# - Sort the array and remove any duplicate
# - Send all LBAs to C code to 
#   - Build TAVL tree sorted in LBA order
#   - Build SG groups with TAVL tree
# - Convert to x,y,z array
#   - Convert LBA to SG (then angular) and track - Use C function getPhyFromLba()
#   - Convert angular and track to x,y,z (This is just radius * np.cos(), radius * np.sin() and track
# - Start with 'the current' set to the lowest LBA, set target_decided flag to false
# - Convert current LBA to current_angular and current_track
# - Animate with update()
#
# - update()
#   - if !target_decided
#       - Select the next point and get the LBA of that point - Use C function selectTarget(). C code removes the entry in LBA TAVL tree and SG group TAVL tree.
#       - Find the index of that target and set the target_angular and target_track
#       - Set target_decided to true
#       - Append an element with a random LBA to LBA numpy array
#       - Add the new LBA to C code by calling C function addLBA() which does the followings.
#           - Insert into LBA TAVL tree and SG group TAVL tree
#       - Convert the random LBA to angular and track (by calling C function getPhyFromLba()) and append elements to angular, track numpy array
#   - Convert angular and track to x,y,z numpy array
#   - Plot the drum
#   - Plot the arrow (from the current to target)
#   - if target angular == 0
#       - remove the element with the current LBA from LBA numpy array, angular, track array
#       - current angular, track = target angular, track
#       - set target_decided to false

# C code :
# void initCache(int maxNode)
#   - Initializes everything
#   - Input : maxNode
#
# void addLba(unsigned lba, unsigned num_of_blocks)
#   - Receives an LBA and allocate an entry
#   - Converts LBA into SG and track and set the LBA, SG, track fields in the entry
#   - Insert the entry into TAVL tree
#   - Insert the entry into SG group TAVL tree
#   - Input : LBA, number of blocks
#   - Output : None
#
# void getPhyFromLba(unsigned lba, unsigned *pSg, unsigned *pTrack)
#   - Receives an LBA and returns SG and track
#   - Input : LBA
#   - Output : SG, track
#
# void selectTargetLba(unsigned *pTargetLba, unsigned *pDistance)
#   - Searches the best target from the current position
#   - Returns the LBA & the distance
#
# void completeTarget(unsigned targetLba)
#   - Removes the node with the given LBA from TAVL tree and SG TAVL tree
#	- Updates the current to the given target
#	- Note : The node is not given so the node needs to be searched using the target LBA.
#

import ctypes
import platform
import numpy as np
import matplotlib.pyplot as plt
import mpl_toolkits.mplot3d as mplot3d
from matplotlib.patches import FancyArrowPatch
from mpl_toolkits.mplot3d import proj3d
import matplotlib.animation as animation
import reorderlib




class Arrow3D(FancyArrowPatch):
    def __init__(self, xs, ys, zs, *args, **kwargs):
        super().__init__((0,0), (0,0), *args, **kwargs)
        self._verts3d = xs, ys, zs

    def do_3d_projection(self, renderer=None):
        xs3d, ys3d, zs3d = self._verts3d
        xs, ys, zs = proj3d.proj_transform(xs3d, ys3d, zs3d, self.axes.M)
        self.set_positions((xs[0],ys[0]),(xs[1],ys[1]))
        return np.min(zs)


# Number of random points
num_points = 2000

# Initialize the reorderLib with the given number of points.
reorderOperator = reorderlib.reorderLibOperator(num_points)
# Then get the geometry information.
numOfBlocks = reorderOperator.getNumOfBlocks()
numOfSgs=reorderOperator.getNumOfSgs()
numOfTracks=reorderOperator.getNumOfTracks()

'''
sg, track = reorderOperator.convertLbaToPhy(0x12345678)

# Print the results
print(f"sg: {sg}")
print(f"track: {track}")
'''

# Generate random LBAs & sort.
lbas=np.random.uniform(0,numOfBlocks,num_points)
lbas=lbas.astype(np.uint32)
lbas.sort()

# Remove duplicate entries
lbas=np.unique(lbas)

# Create 2 empty np arrays. angles & x
sg_and_track=np.empty((1,2),dtype=np.uint32)
sg_and_track=np.delete(sg_and_track,0,0)
# Why can't I use sg_and_track=np.array([[]])?

# Added each entry into the cache (both TAVL tree and SG TAVL tree)
for lba in lbas:
    reorderOperator.addLba(lba, 1)
    sg,track=reorderOperator.convertLbaToPhy(lba)
    sg_and_track=np.vstack([sg_and_track,[sg,track]])

sg=np.hsplit(sg_and_track,2)[0]
track=np.hsplit(sg_and_track,2)[1]

# Convert SG into angles
angles=sg*2*np.pi/numOfSgs


# Cylinder parameters
radius = 1.0
height = 2.0
#track=track*height/numOfTracks

# Angular velocity of the drum rotation (in SG)
frames_per_sg=1
angular_velocity=-1/frames_per_sg

'''
# Generate random angles for the points
# angles = np.random.uniform(0, 2*np.pi, num_points)
'''
# Convert current LBA (which is 0) to current_angular and current_track
currentSg,currentTrack=reorderOperator.convertLbaToPhy(0)
targetLba=0;
targetSg=0
targetTrack=0
target_decided=False

# Function to update the plot for each frame of the animation
# - update()
#   - if !target_decided
#       - Select the next point and get the LBA of that point - Use C function selectTarget(). C code removes the entry in LBA TAVL tree and SG group TAVL tree.
#       - Find the index of that target and set the target_angular and target_track
#       - Set target_decided to true
#   - Convert angular and track to x,y,z numpy array
#   - Plot the drum
#   - Plot the arrow (from the current to target)
#   - if target angular == 0
#       - remove the element with the current LBA from LBA numpy array, angular, track array
#       - current angular, track = target angular, track
#       - set target_decided to false
#       - Append an element with a random LBA to LBA numpy array
#       - Add the new LBA to C code by calling C function addLBA() which does the followings.
#           - Insert into LBA TAVL tree and SG group TAVL tree
#       - Convert the random LBA to angular and track (by calling C function getPhyFromLba()) and append elements to angular, track numpy array
def update(frame):
    global target_decided,currentSg,currentTrack,targetLba,targetSg,targetTrack,lbas,angles,track
    if target_decided==False:
        targetLba,distance=reorderOperator.selectTarget()
        targetSg,targetTrack=reorderOperator.convertLbaToPhy(targetLba)
        target_decided=True

    # Calculate the new angles based on the angular velocity
    angluar_delta=angular_velocity*2*np.pi/numOfSgs*frame
    new_angles=angles+angluar_delta
    
    # Calculate the y, and z coordinates of the points on the drum
    x=np.linspace(0, numOfTracks, num_points)
    y=radius*-np.cos(new_angles)
    z=radius*np.sin(new_angles)
    
    # Clear the previous plot
    ax.cla()
    
    # Plot the drum as a cylinder in 3D
    u=np.linspace(0, 2*np.pi, 100)
    v=np.linspace(0, numOfTracks, 100)
    u,v=np.meshgrid(u,v)
    z_drum=radius*np.sin(u)
    y_drum=radius*-np.cos(u)
    x_drum=v
    ax.plot_surface(x_drum, y_drum, z_drum, color='black', alpha=0.3)
    
    # Plot the random points on the drum
    ax.scatter(track, y, z, color='blue', s=5)

    # Draw an arrow from the current to target
    new_current_angle=(currentSg*2*np.pi/numOfSgs)+angluar_delta;
    current_x=currentTrack
    current_y=radius*-np.cos(new_current_angle)
    current_z=radius*np.sin(new_current_angle)
    new_target_angle=(targetSg*2*np.pi/numOfSgs)+angluar_delta;
    target_x=targetTrack
    target_y=radius*-np.cos(new_target_angle)
    target_z=radius*np.sin(new_target_angle)
    # ax.quiver(current_x,current_y,current_z,target_x-current_x,target_y-current_y,target_z-current_z,color='red',alpha = .2,lw = 2,)
    a = Arrow3D([current_x, target_x], [current_y, target_y], [current_z, target_z], mutation_scale=20, lw=2, arrowstyle="-|>", color="r")
    ax.add_artist(a)

    # Set the plot limits and aspect ratio
    #ax.set_zlim3d(-radius, radius)
    #ax.set_ylim3d(-radius, radius)
    ax.set_xlim3d(0, numOfTracks)
    ax.set_box_aspect([1,1,1])
    
    # Set the title
    ax.set_title('Cylinder Drum')

    # if target angular == 0
    #   remove the element with the current LBA from LBA numpy array, angular, track array
    #   Complete the target
    #   current angular, track = target angular, track
    #   set target_decided to false
    #   Append an element with a random LBA to LBA numpy array
    #   Add the new LBA to C code by calling C function addLBA() which does the followings.
    #       Insert into LBA TAVL tree and SG group TAVL tree
    #   Convert the random LBA to angular and track (by calling C function getPhyFromLba()) and append elements to angular, track numpy array
    if target_y<-np.cos(2*np.pi/numOfSgs):
        # remove the element with the current LBA from LBA numpy array, angular, track array
        #target_index=lbas.index(targetLba)
        target_index=np.where(lbas==targetLba)[0]
        lbas=np.delete(lbas,target_index)
        angles=np.delete(angles,target_index)
        track=np.delete(track,target_index)
        # current angular, track = target angular, track
        currentSg=targetSg
        currentTrack=targetTrack
        target_decided=False
        reorderOperator.completeLba(targetLba)
        # Append an element with a random LBA to LBA numpy array
        newLba=np.random.randint(0,numOfBlocks)
        lbas=np.append(lbas, [newLba])

        # Sort and find the index of this new LBA
        lbas.sort()
        new_index=np.where(lbas==newLba)[0]
        newSg,newTrack=reorderOperator.convertLbaToPhy(newLba)
        newAngle=newSg*2*np.pi/numOfSgs
        angles=np.insert(angles,new_index,newAngle)
        track=np.insert(track,new_index,newTrack)
        reorderOperator.addLba(newLba,1)

# Before starting the animation, process about half of the entries so that the animation can start in the middle.
half_points=num_points-100
for k in range(1, half_points):
    targetLba,distance=reorderOperator.selectTarget()
    target_index=np.where(lbas==targetLba)[0]
    lbas=np.delete(lbas,target_index)
    angles=np.delete(angles,target_index)
    track=np.delete(track,target_index)
    reorderOperator.completeLba(targetLba)
    # Append an element with a random LBA to LBA numpy array
    newLba=np.random.randint(0,numOfBlocks)
    lbas=np.append(lbas, [newLba])
    # Sort and find the index of this new LBA
    lbas.sort()
    new_index=np.where(lbas==newLba)[0]
    newSg,newTrack=reorderOperator.convertLbaToPhy(newLba)
    newAngle=newSg*2*np.pi/numOfSgs
    angles=np.insert(angles,new_index,newAngle)
    track=np.insert(track,new_index,newTrack)
    reorderOperator.addLba(newLba,1)

currentSg,currentTrack=reorderOperator.convertLbaToPhy(targetLba)

# Create the figure and axis
fig = plt.figure()
ax = fig.add_subplot(111, projection='3d')

numOfFrames=numOfSgs*frames_per_sg
# Create the animation
ani = animation.FuncAnimation(fig, update, frames=numOfFrames, interval=0, save_count=1500) # increase frame to avoid resetting/repeating  

# Uncomment the following line to save animated gif.
ani.save('animation.gif', writer='imagemagick', fps=30)

# Display the animation
plt.show()


