import boto3
import time

# noinspection PyUnresolvedReferences
session = boto3.Session(profile_name='ecs')
ec2 = session.client('ec2')


# https://boto3.amazonaws.com/v1/documentation/api/latest/reference/services/ec2.html#EC2.Client.run_instances
def run_new_server():
    with open('cloud-init.sh', "rt") as cloud_init:
        # http://fbrnc.net/blog/2015/11/how-to-provision-an-ec2-instance
        user_data = "".join(cloud_init.readlines())

    response = ec2.run_instances(
        #ImageId='ami-0ec1ba09723e5bfac',  # Got this via console, Launch Instance wizard. This is AWS Linux 2
        ImageId='ami-0b418580298265d5c',  # This is Ubuntu 18.04 LTS
        InstanceType='m5a.large',
        AdditionalInfo='JammerNetz auto-created',
        MaxCount=1,
        MinCount=1,
        Monitoring={
            'Enabled': False
        },
        KeyName='AWS photogenity',  # Required for SSH access
            UserData=user_data,
        SecurityGroupIds=[
            # 'sg-69908406',
            'sg-016f395831eedd70a'
        ], )

    print(response)
    instance_id = response['Instances'][0]['InstanceId']
    print("Instance ID", instance_id)
    return instance_id


def get_instance_info(instance_id):
    response = ec2.describe_instances()
    # print(response)
    reservations = response['Reservations']
    for r in reservations:
        for instance in r['Instances']:
            if instance['InstanceId'] == instance_id:
                return instance
    return None


def instance_state(instance_id):
    data = get_instance_info(instance_id)
    return data['State']


def instance_status(instance_id):
    response = ec2.describe_instance_status()
    for status in response['InstanceStatuses']:
        if status['InstanceId'] == instance_id:
            return status['InstanceStatus']['Status']

# Create a new server
instanceID = run_new_server()

# Wait for the state to become "running"
running = False
print("Waiting for instance to be in running state...", end='')
while not running:
    state = instance_state(instanceID)
    if state['Name'] == 'running':
        running = True
    else:
        print(".", end='')
        time.sleep(0.2)

initialized = False
print("Waiting for instance to be done initializing...", end='')
while not initialized:
    status = instance_status(instanceID)
    if status != 'ok':
        print('.', end='')
        time.sleep(0.5)
    else:
        initialized = True
