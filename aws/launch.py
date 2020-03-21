import boto3
import time
import os
import uuid

# noinspection PyUnresolvedReferences
session = boto3.Session(profile_name='ecs')
ec2 = session.client('ec2')
ec2_res = session.resource('ec2')


# https://gist.github.com/nguyendv/8cfd92fc8ed32ebb78e366f44c2daea6
def create_vpc(jammernetz_run_id):
    vpc = ec2_res.create_vpc(CidrBlock='192.168.0.0/16')
    vpc.create_tags(Tags=[{"Key": "Name", "Value": "jammernetz-" + jammernetz_run_id}])
    vpc.wait_until_available()
    print("Created VPC with id", vpc.id)

    # create then attach internet gateway
    ig = ec2_res.create_internet_gateway()
    vpc.attach_internet_gateway(InternetGatewayId=ig.id)
    print("Created Internet Gateway with id", ig.id)

    # create a route table and a public route
    route_table = vpc.create_route_table()
    route = route_table.create_route(
        DestinationCidrBlock='0.0.0.0/0',
        GatewayId=ig.id
    )
    print("Created route table with id", route_table.id)

    # create subnet
    subnet = ec2_res.create_subnet(CidrBlock='192.168.1.0/24', VpcId=vpc.id)
    print("Created subnet with id", subnet.id)

    # associate the route table with the subnet
    route_table.associate_with_subnet(SubnetId=subnet.id)
    return vpc.id, subnet.id


def delete_vpc(vpc_id):
    response = ec2.delete_vpc(vpc_id)
    print(response)


# https://blog.ipswitch.com/how-to-create-an-ec2-instance-with-python
def create_key_pair(key_name):
    filename = key_name + '.pem'
    with open(filename, 'w') as outfile:
        key_pair = ec2_res.create_key_pair(KeyName=key_name)
        key_pair_content = str(key_pair.key_material)
        print("Created key pair", key_name)
        outfile.write(key_pair_content)
    os.chmod(filename, 0o400)
    print("Stored keys in file", filename)


def delete_key_pair(key_name):
    filename = key_name + '.pem'
    response = ec2.delete_key_pair(KeyName=key_name)
    print(response)
    os.chmod(filename, 0o600)
    os.remove(filename)
    print("Deleted key pair", key_name, "and deleted file", filename)


def add_inbound_rules(group):
    response = group.authorize_ingress(
        CidrIp='0.0.0.0/0',
        ToPort=22,
        FromPort=22,
        IpProtocol='tcp'
    )
    print(response)
    response = group.authorize_ingress(
        CidrIp='0.0.0.0/0',
        ToPort=7777,
        FromPort=7777,
        IpProtocol='udp'
    )
    print(response)


def create_security_group(group_name, vpc_id):
    new_group = ec2_res.create_security_group(
        Description="Auto generated security group for JammerNetz",
        GroupName=group_name,
        VpcId=vpc_id)
    group_id = new_group.id
    print("Created security group", group_name, "with id", group_id)
    add_inbound_rules(new_group)
    return group_id


def delete_security_group(group_id):
    print("Removing security group", group_id)
    response = ec2.delete_security_group(GroupId=group_id)
    print(response)


# https://boto3.amazonaws.com/v1/documentation/api/latest/reference/services/ec2.html#EC2.Client.run_instances
def run_new_server(keyname, security_group_id, subnet_id):
    with open('cloud-init.sh', "rt") as cloud_init:
        # http://fbrnc.net/blog/2015/11/how-to-provision-an-ec2-instance
        user_data = "".join(cloud_init.readlines())

        response = ec2.run_instances(
            ImageId='ami-0b418580298265d5c',  # This is Ubuntu 18.04 LTS
            InstanceType='m5a.large',
            AdditionalInfo='JammerNetz auto-created',
            MaxCount=1,
            MinCount=1,
            Monitoring={
                'Enabled': False
            },
            KeyName=keyname,  # Required for potential SSH access
            UserData=user_data,
            NetworkInterfaces=[{'SubnetId': subnet_id, 'DeviceIndex': 0, 'AssociatePublicIpAddress': True,
                                'Groups': [security_group_id]}]
        )

        print(response)
    instance_id = response['Instances'][0]['InstanceId']
    print("Instance ID", instance_id)
    return instance_id


def terminate_server(instance_id):
    response = ec2.terminate_instances(
        InstanceIds=[
            instance_id,
        ],
        DryRun=False
    )
    print(response)


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


def instance_public_ip(instance_id):
    data = get_instance_info(instance_id)
    return data['PublicIpAddress'], data['PublicDnsName']


# Create a keypair should we want to ssh into the machine later.
# Append 8 random bytes to not create conflicts with stale key pairs
jammernetz_run_id = str(uuid.uuid4())[:8]

vpc_id, subnet_id = create_vpc(jammernetz_run_id)

key_pair_name = 'jammernetz-keys-' + jammernetz_run_id
create_key_pair(key_pair_name)

# Create security group
security_group_id = create_security_group('jammernetz-sg-' + jammernetz_run_id, vpc_id)

# Create a new server
instanceID = run_new_server(key_pair_name, security_group_id, subnet_id)
#instanceID = 'i-0d22be14c8936a40b'

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
print()

initialized = False
print("Waiting for instance to be done initializing...", end='')
while not initialized:
    status = instance_status(instanceID)
    if status != 'ok':
        print('.', end='')
        time.sleep(0.5)
    else:
        initialized = True
print()

# Reporting public IP here?
print(instance_public_ip(instanceID))

input("Press Enter to terminate the instance or CTRL-C to keep it...")

print("Terminating instance")
terminate_server(instanceID)

print("Deleting key pair")
delete_key_pair(key_pair_name)

#print("Deleting security group")
#delete_security_group(security_group_id)

print("Deleting VPC")
delete_vpc(vpc_id)
